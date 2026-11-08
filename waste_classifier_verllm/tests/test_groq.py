from types import SimpleNamespace
from unittest.mock import AsyncMock

from fastapi import HTTPException
from fastapi.testclient import TestClient
import pytest

from backend.app import Settings, create_app
from backend.quota import GroqQuota
from test_backend import send


def groq_api(text='{"label":"plastic"}', finish='stop'):
    create = AsyncMock(return_value=SimpleNamespace(choices=[SimpleNamespace(
        finish_reason=finish, message=SimpleNamespace(content=text))]))
    return SimpleNamespace(chat=SimpleNamespace(completions=SimpleNamespace(create=create)))


def settings(tmp_path, **kwargs):
    return Settings(provider='groq', mock=False, model='qwen/qwen3.8-27b',
                    quota_path=str(tmp_path / 'quota.sqlite3'), **kwargs)


def test_groq_image_payload_and_cooldown(tmp_path):
    api = groq_api()
    with TestClient(create_app(settings(tmp_path), api)) as client:
        assert send(client).json() == {'label': 'plastic'}
        response = send(client)
        assert response.status_code == 429
        assert 1 <= int(response.headers['retry-after']) <= 30
    assert api.chat.completions.create.call_count == 1
    payload = api.chat.completions.create.call_args.kwargs
    assert payload['response_format'] == {'type': 'json_object'}
    assert payload['reasoning_effort'] == 'none'
    assert payload['max_completion_tokens'] == 100
    image = payload['messages'][1]['content'][1]['image_url']
    assert image['url'].startswith('data:image/jpeg;base64,')


@pytest.mark.parametrize('text,finish', [('{"label":"glass"}', 'stop'),
    ('not json', 'stop'), ('{"label":"paper"}', 'length'),
    ('{"label":"metal","score":1}', 'stop'), (None, 'stop')])
def test_groq_invalid_output(tmp_path, text, finish):
    with TestClient(create_app(settings(tmp_path), groq_api(text, finish))) as client:
        assert send(client).status_code == 502


def test_rolling_daily_quota_survives_restart(tmp_path):
    path = str(tmp_path / 'quota.db')
    quota = GroqQuota(path, interval=30, daily_limit=2)
    quota.reserve(now=100000)
    with pytest.raises(HTTPException) as exc:
        quota.reserve(now=100001)
    assert exc.value.headers['Retry-After'] == '29'
    assert exc.value.detail == 'Groq demo quota/cooldown reached'
    quota.reserve(now=100030)
    restarted = GroqQuota(path, interval=30, daily_limit=2)
    with pytest.raises(HTTPException) as exc:
        restarted.reserve(now=100060)
    assert exc.value.headers['Retry-After'] == '86340'
    assert exc.value.detail == 'daily_quota'
    assert restarted.daily_block(now=100060) == 86340
    restarted.reserve(now=186400)


def test_groq_429_backoff_persisted(tmp_path):
    api = groq_api()
    error = RuntimeError('upstream rate limit')
    error.status_code = 429
    error.response = SimpleNamespace(headers={'retry-after': '120'})
    api.chat.completions.create.side_effect = error
    config = settings(tmp_path)
    with TestClient(create_app(config, api)) as client:
        response = send(client)
        assert response.status_code == 429
        assert response.headers['retry-after'] == '120'
    with TestClient(create_app(config, groq_api())) as client:
        response = send(client)
        assert response.status_code == 429
        assert int(response.headers['retry-after']) > 100


def test_mock_bypasses_groq_quota(tmp_path):
    config = Settings(provider='groq', mock=True, quota_path=str(tmp_path / 'unused.db'))
    with TestClient(create_app(config)) as client:
        for _ in range(3):
            assert send(client).status_code == 200
    assert not (tmp_path / 'unused.db').exists()
