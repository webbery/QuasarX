from tool import check_response, BASE_URL
import pytest
import requests

@pytest.mark.usefixtures("auth_token")
class TestUser:
    @pytest.mark.timeout(5)
    def test_status(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        response = requests.get(f"{BASE_URL}/server/status", **kwargs)
        data = check_response(response)
        assert 'cpu' in data
        assert 'mem' in data

    @pytest.mark.timeout(20)
    def test_index(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        response = requests.get(f"{BASE_URL}/index/quote", **kwargs)
        data = check_response(response)
        assert len(data) > 0
        assert 'code' in data[0]
        assert 'price' in data[0]
        assert 'rate' in data[0]

    @pytest.mark.timeout(5)
    def test_add_exchange(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        json = {
            'type': 2,
            'data': {
                'name': 'XTP-test',
                'account': '400012020102',
                'passwd': '111111',
                "api": "xtp",
                "key": "dsdfsfserweh345"
            }
        }
        response = requests.post(f"{BASE_URL}/server/config", json=json, **kwargs)
        check_response(response)

    @pytest.mark.timeout(5)
    def test_delete_exchange(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        json = {
            'type': 4,
            'data': {
                'name': 'XTP-test'
            }
        }
        response = requests.post(f"{BASE_URL}/server/config", json=json, **kwargs)
        check_response(response)

    @pytest.mark.timeout(5)
    def test_get_config(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        response = requests.get(f"{BASE_URL}/server/config", **kwargs)
        data = check_response(response)
        assert 'server' in data
        assert 'default' in data['server']
        assert 'exchange' in data
        assert len(data['exchange']) > 0

    @pytest.mark.timeout(5)
    def test_update_smtp(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        json = {
            'type': 7,
            'data': {
                'addr': 'smtp.qq.com:587',
                'auth': '11111111111',
                'mail': '222222@foxmail.com'
            }
        }
        response = requests.post(f"{BASE_URL}/server/config", json=json, **kwargs)
        check_response(response)

    @pytest.mark.timeout(5)
    def test_update_passwd(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        json = {
            'type': 5,
            'data': {
                'org': 'admin',
                'latest': 'admin'
            }
        }
        response = requests.post(f"{BASE_URL}/server/config", json=json, **kwargs)
        check_response(response)

    # @pytest.mark.timeout(5)
    # def test_update_commission(self, auth_token):
    #     kwargs = {
    #         'verify': False  # 始终禁用 SSL 验证
    #     }
    #     if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
    #         kwargs['headers'] = {'Authorization': auth_token}
    #     json = {
    #         'type': 6,
    #         'data': {
    #             'org': 'admin',
    #             'latest': 'admin'
    #         }
    #     }
    #     response = requests.post(f"{BASE_URL}/server/config", json=json, **kwargs)
    #     check_response(response)

    @pytest.mark.timeout(5)
    def test_update_schedule(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        json = {
            'type': 8,
            'data': {
                'daily': '20:00',
            }
        }
        response = requests.post(f"{BASE_URL}/server/config", json=json, **kwargs)
        check_response(response)

    @pytest.mark.timeout(5)
    def test_get_position(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        response = requests.get(f"{BASE_URL}/position", **kwargs)
        data = check_response(response)
        assert isinstance(data, list)
        for item in data:
            assert 'id' in item
            assert 'datetime' in item
            assert 'operation' in item
            assert 'price' in item
            assert 'quantity' in item
            break