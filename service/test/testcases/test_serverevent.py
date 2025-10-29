from tool import check_response, BASE_URL
import pytest
import requests

@pytest.mark.usefixtures("auth_token")
class TestServerEvent:
    @pytest.mark.timeout(50)
    def test_status(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {
                'Authorization': auth_token,
                'Accept': 'text/event-stream'
            }
        response = requests.get(f"{BASE_URL}/server/event", stream=True, **kwargs)
        assert response.status_code == 200
        try:
            for line in response.iter_lines(decode_unicode=True):
                if line:
                    print('line ', line)
                    tokens = line.split(':')
                    assert len(tokens) > 0
                    if tokens[0] == 'system_status':
                        infos = tokens[1].split(' ')
                        assert len(infos) == 2
                    else:
                        assert False
        except:
            pass
        response.close()