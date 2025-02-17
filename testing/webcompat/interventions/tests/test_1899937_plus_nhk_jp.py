import time

import pytest

URL = "https://plus.nhk.jp/"
UNSUPPORTED_CSS = "#for_firefox"
DENIED_TEXT = "This webpage is not available in your area"


async def check_site(client, should_pass):
    await client.navigate(URL, wait="complete")
    time.sleep(3)
    if client.find_text(DENIED_TEXT):
        pytest.skip("Region-locked, cannot test. Try using a VPN set to Japan.")
        return

    unsupported = client.find_css(UNSUPPORTED_CSS)
    assert (should_pass and not client.is_displayed(unsupported)) or (
        not should_pass and client.is_displayed(unsupported)
    )


@pytest.mark.asyncio
@pytest.mark.with_interventions
async def test_enabled(client):
    await check_site(client, should_pass=True)


@pytest.mark.asyncio
@pytest.mark.without_interventions
async def test_disabled(client):
    await check_site(client, should_pass=False)
