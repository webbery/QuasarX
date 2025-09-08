import requests
import sys
from tool import check_response, BASE_URL
import pytest

@pytest.mark.usefixtures("auth_token")
class TestData:

    @pytest.mark.timeout(10)
    def test_data_sync(self, auth_token):
        headers = {
            'Authorization': auth_token
        }
        response = requests.get(f"{BASE_URL}/data/sync", stream=True, headers=headers, verify=False)
        response.raise_for_status()

        assert 'chunked' in response.headers.get('Transfer-Encoding', '').lower()