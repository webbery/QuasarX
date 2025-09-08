from tool import check_response, BASE_URL
import pytest
import requests

@pytest.mark.usefixtures("auth_token")
class TestUser:
    @pytest.mark.timeout(5)
    def test_status(self, auth_token):
        headers = {
            'Authorization': auth_token
        }
        response = requests.get(f"{BASE_URL}/server/status", headers=headers, verify=False)
        data = check_response(response)
        assert 'cpu' in data
        assert 'mem' in data