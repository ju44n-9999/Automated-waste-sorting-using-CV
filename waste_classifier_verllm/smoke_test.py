"""Send exactly one local JPEG through the real Groq backend; no servo commands."""
import argparse
import asyncio
from dataclasses import replace
import json
import os
from pathlib import Path
import time

import httpx
from openai import AsyncOpenAI

from backend.app import Settings, create_app


async def main(image_path: Path):
    settings = replace(Settings.from_env(), mock=False)
    if settings.provider != 'groq':
        raise SystemExit('Set LLM_PROVIDER=groq for this smoke test.')
    key = os.getenv('GROQ_API_KEY', '').strip()
    if not key:
        raise SystemExit('GROQ_API_KEY is empty; fill in .env first.')
    image = image_path.read_bytes()
    observed = {}
    async with AsyncOpenAI(api_key=key, base_url='https://api.groq.com/openai/v1',
                           timeout=settings.timeout, max_retries=0) as sdk:
        original_create = sdk.chat.completions.create

        async def observe(**kwargs):
            try:
                result = await original_create(**kwargs)
            except Exception as exc:
                observed['upstream_error_type'] = type(exc).__name__
                observed['upstream_http_status'] = getattr(exc, 'status_code', None)
                body = getattr(exc, 'body', None)
                if isinstance(body, dict):
                    error = body.get('error', body)
                    if isinstance(error, dict):
                        observed['upstream_error_code'] = error.get('code')
                raise
            observed['upstream_model'] = result.model
            observed['usage'] = result.usage.model_dump() if result.usage else None
            observed['raw_model_content'] = result.choices[0].message.content
            return result

        sdk.chat.completions.create = observe
        app = create_app(settings, sdk)
        started = time.perf_counter()
        async with app.router.lifespan_context(app):
            async with httpx.AsyncClient(transport=httpx.ASGITransport(app=app),
                                         base_url='http://smoke-test') as client:
                response = await client.post('/classify', content=image, headers={
                    'Content-Type': 'image/jpeg', 'X-Device-Token': settings.token})
        report = {'image': str(image_path), 'image_bytes': len(image),
                  'configured_model': settings.model, 'mock': False,
                  'timeout_seconds': settings.timeout,
                  'http_status': response.status_code,
                  'elapsed_seconds': round(time.perf_counter() - started, 3),
                  'backend_response': response.json(), **observed}
        print(json.dumps(report, ensure_ascii=True, indent=2))
        if response.status_code != 200:
            raise SystemExit(1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    asyncio.run(main(parser.parse_args().image))
