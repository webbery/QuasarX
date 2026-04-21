import requests
import pytest
from tool import check_response, BASE_URL

@pytest.mark.usefixtures("auth_token")
class TestShibor:
    """SHIBOR 数据测试用例"""

    @pytest.mark.timeout(15)
    def test_get_today_shibor(self, auth_token):
        """
        测试 /market/shibor 接口 - 获取当天 SHIBOR 数据
        验证返回数据结构，输出所有期限利率
        """
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        # 发送请求（不指定 date，默认返回最新数据）
        response = requests.get(f"{BASE_URL}/market/shibor", **kwargs)
        data = check_response(response)

        # 验证基本响应结构
        assert isinstance(data, dict)
        assert data["status"] == "success"
        assert "date" in data
        assert "count" in data
        assert "data" in data
        assert isinstance(data["data"], list)

        # 验证数据非空
        shibor_list = data["data"]
        assert len(shibor_list) > 0, "SHIBOR 数据不应为空"

        # 输出统计结果
        print(f"\n{'='*60}")
        print(f"SHIBOR 数据 ({data['date']})")
        print(f"{'='*60}")
        print(f"{'期限':<10} {'利率(%)':<12} {'涨跌(BP)':<10}")
        print("-" * 60)

        for item in shibor_list:
            term = item.get("term", "")
            rate = item.get("rate", 0)
            change = item.get("change", 0)
            change_str = f"{change:+.1f}" if change != 0 else "-"
            print(f"{term:<10} {rate:<12.4f} {change_str:<10}")

        print(f"{'='*60}")
        print(f"共 {data['count']} 个期限")

        # 验证期限完整性
        expected_terms = ["隔夜", "1周", "2周", "1个月", "3个月", "6个月", "9个月", "1年"]
        actual_terms = [item["term"] for item in shibor_list]
        
        for term in expected_terms:
            assert term in actual_terms, f"缺少期限: {term}"

    @pytest.mark.timeout(10)
    def test_get_shibor_by_term(self, auth_token):
        """
        测试 /market/shibor?term=1M - 按期限过滤
        """
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        # 测试 1个月 SHIBOR
        response = requests.get(f"{BASE_URL}/market/shibor?term=1个月", **kwargs)
        data = check_response(response)

        assert data["status"] == "success"
        assert data["count"] == 1, "按期限过滤应只返回1条数据"
        assert data["data"][0]["term"] == "1个月"

        print(f"\n1个月 SHIBOR: {data['data'][0]['rate']}%")

    @pytest.mark.timeout(10)
    def test_get_shibor_all_terms_rates(self, auth_token):
        """
        获取所有期限利率并输出利率曲线
        """
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        response = requests.get(f"{BASE_URL}/market/shibor", **kwargs)
        data = check_response(response)

        assert data["status"] == "success"

        # 构建期限-利率映射
        rate_map = {}
        for item in data["data"]:
            rate_map[item["term"]] = item["rate"]

        # 输出利率曲线
        term_order = ["隔夜", "1周", "2周", "1个月", "3个月", "6个月", "9个月", "1年"]
        
        print(f"\n{'='*60}")
        print(f"SHIBOR 利率曲线 ({data['date']})")
        print(f"{'='*60}")
        
        for term in term_order:
            if term in rate_map:
                rate = rate_map[term]
                bar = "█" * int(rate * 10)  # 可视化
                print(f"{term:<10} {rate:.4f}%  {bar}")
        
        print(f"{'='*60}")
