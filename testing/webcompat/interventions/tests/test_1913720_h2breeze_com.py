import pytest

URL = "https://h2breeze.com/"

SUCCESS_CSS = "#content"
BLOCKED_TEXT = "404 Not Found"


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_css(SUCCESS_CSS, is_displayed=True)
    assert not client.find_text(BLOCKED_TEXT, is_displayed=True)


@pytest.mark.skip_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await client.navigate(URL, wait="none")
    assert client.await_text(BLOCKED_TEXT, is_displayed=True)
    assert not client.find_css(SUCCESS_CSS, is_displayed=True)


@pytest.mark.only_platforms("android")
@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled_mobile(client):
    await client.navigate(URL, wait="none")
    assert client.await_css(SUCCESS_CSS, is_displayed=True)
    assert not client.find_text(BLOCKED_TEXT, is_displayed=True)
