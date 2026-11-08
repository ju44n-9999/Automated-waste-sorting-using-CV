import asyncio
from io import BytesIO
from types import SimpleNamespace
from unittest.mock import AsyncMock

from fastapi.testclient import TestClient
from PIL import Image
import pytest

from backend.app import Settings, create_app


def jpeg():
    out = BytesIO()
    Image.new('RGB', (32, 24), 'white').save(out, 'JPEG')
    return out.getvalue()


def send(client, data=None, **kwargs):
    return client.post('/classify', content=jpeg() if data is None else data,
                       headers={'Content-Type': 'image/jpeg'}, **kwargs)


@pytest.mark.parametrize('label', ['plastic', 'metal', 'paper', 'unknown'])
def test_mock(label):
    with TestClient(create_app(Settings(mock_label=label))) as client:
        assert send(client).json() == {'label': label}


@pytest.mark.parametrize('data', [b'', b'not jpeg', jpeg()[:50], jpeg()[:-20]])
def test_bad_image(data):
    with TestClient(create_app(Settings())) as client:
        assert send(client, data).status_code == 400


def test_upload_limits_and_auth():
    with TestClient(create_app(Settings(max_bytes=100))) as client:
        assert send(client).status_code == 413
    with TestClient(create_app(Settings(max_pixels=100))) as client:
        assert send(client).status_code == 413
    with TestClient(create_app(Settings())) as client:
        assert client.post('/classify', content=jpeg()).status_code == 415
    with TestClient(create_app(Settings(token='test-token'))) as client:
        assert send(client).status_code == 401
        assert client.post('/classify', content=jpeg(), headers={
            'Content-Type': 'image/jpeg', 'X-Device-Token': 'test-token'
        }).status_code == 200


def fake_api(text='{"label":"paper"}', status='completed'):
    call = AsyncMock(return_value=SimpleNamespace(output_text=text, status=status))
    return SimpleNamespace(responses=SimpleNamespace(create=call))


def test_openai_payload_and_result():
    api = fake_api()
    with TestClient(create_app(Settings(mock=False), api)) as client:
        assert send(client).json() == {'label': 'paper'}
    payload = api.responses.create.call_args.kwargs
    assert payload['model'] == 'gpt-4.1-mini'
    assert payload['store'] is False
    assert payload['text']['format']['strict'] is True
    schema = payload['text']['format']['schema']
    assert schema['additionalProperties'] is False
    assert set(schema['properties']['label']['enum']) == {'plastic', 'metal', 'paper', 'unknown'}
    image = payload['input'][0]['content'][0]
    assert image['detail'] == 'low'
    assert image['image_url'].startswith('data:image/jpeg;base64,')


@pytest.mark.parametrize('text,status', [
    ('{"label":"glass"}', 'completed'), ('not json', 'completed'),
    ('{"label":"plastic","confidence":0.9}', 'completed'),
    ('', 'completed'), ('{"label":"metal"}', 'incomplete'),
])
def test_invalid_model_output(text, status):
    with TestClient(create_app(Settings(mock=False), fake_api(text, status))) as client:
        assert send(client).status_code == 502
        assert send(client).status_code == 502  # Busy latch released after error.


@pytest.mark.parametrize('status,expected', [(429, 429), (500, 502), (401, 502)])
def test_upstream_error(status, expected):
    api = fake_api()
    exc = RuntimeError('Secret upstream error must not reach client')
    exc.status_code = status
    api.responses.create.side_effect = exc
    with TestClient(create_app(Settings(mock=False), api)) as client:
        response = send(client)
        assert response.status_code == expected
        assert 'Secret' not in response.text


def test_timeout_and_recovery():
    api = fake_api()

    async def slow(**kwargs):
        await asyncio.sleep(0.1)

    api.responses.create.side_effect = slow
    with TestClient(create_app(Settings(mock=False, timeout=0.01), api)) as client:
        assert send(client).status_code == 504
        api.responses.create.side_effect = None
        assert send(client).status_code == 200


def test_busy_rejects_before_api():
    app = create_app(Settings())
    with TestClient(app) as client:
        app.state.busy = True
        assert send(client).status_code == 429


def test_non_jpeg_with_jpeg_mime():
    out = BytesIO()
    Image.new('RGB', (10, 10)).save(out, 'PNG')
    with TestClient(create_app(Settings())) as client:
        assert send(client, out.getvalue()).status_code == 400
