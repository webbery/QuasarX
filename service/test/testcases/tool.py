
BASE_URL = "http://localhost:19107/v0"
# --------------------------
# 测试工具函数
# --------------------------
def check_response(response, expected_status=200):
    """验证响应状态码和基本结构"""
    assert response.status_code == expected_status
    if expected_status == 200:
        assert isinstance(response.json(), (dict, list))
    return response.json()