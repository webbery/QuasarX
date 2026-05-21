import requests
import pytest
from tool import check_response, BASE_URL

@pytest.mark.usefixtures("auth_token")
class TestSectorQuote:
    """行业板块实时行情测试用例"""

    @pytest.mark.timeout(10)
    def test_sector_quote(self, auth_token):
        """
        测试 /stocks/sector/quote 接口
        验证返回数据结构，并统计上涨和下跌的板块总数量
        """
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        # 发送请求
        response = requests.get(f"{BASE_URL}/stocks/sector/quote", **kwargs)
        data = check_response(response)

        # 验证基本响应结构
        assert isinstance(data, dict)
        assert "status" in data
        assert data["status"] == "success"
        assert "date" in data
        assert "data" in data
        assert isinstance(data["data"], list)

        # 验证返回数据非空
        sector_list = data["data"]
        assert len(sector_list) > 0, "行业板块数据不应为空"

        # 统计上涨和下跌的板块数量
        up_count = 0
        down_count = 0
        flat_count = 0

        for sector in sector_list:
            # 验证每个板块的基本字段
            assert "name" in sector, f"板块数据缺少 name 字段: {sector}"
            assert "change" in sector or "changePercent" in sector or "pctChange" in sector or "change_pct" in sector, \
                f"板块数据缺少涨跌幅字段: {sector}"

            # 获取涨跌幅（兼容不同的字段名）
            change_pct = (
                sector.get("changePercent") or
                sector.get("pctChange") or
                sector.get("change_pct") or
                sector.get("change", 0)
            )

            # 统计涨跌平
            if change_pct > 0:
                up_count += 1
            elif change_pct < 0:
                down_count += 1
            else:
                flat_count += 1

        # 输出统计结果
        total = len(sector_list)
        print(f"\n{'='*60}")
        print(f"行业板块行情统计 (日期: {data['date']})")
        print(f"{'='*60}")
        print(f"板块总数: {total}")
        print(f"上涨数量: {up_count}")
        print(f"下跌数量: {down_count}")
        print(f"平盘数量: {flat_count}")
        print(f"{'='*60}")

        # 验证统计结果的合理性
        assert up_count + down_count + flat_count == total, "上涨+下跌+平盘应等于总数"
        assert up_count >= 0, "上涨数量不应为负"
        assert down_count >= 0, "下跌数量不应为负"

        # 输出部分板块详情（前5个）
        print(f"\n涨跌幅排名前5的板块:")
        sorted_sectors = sorted(
            sector_list,
            key=lambda x: x.get("changePercent") or x.get("pctChange") or x.get("change_pct") or x.get("change", 0),
            reverse=True
        )[:5]
        for i, sector in enumerate(sorted_sectors, 1):
            name = sector.get("name", "未知")
            change_pct = sector.get("changePercent") or sector.get("pctChange") or sector.get("change_pct") or sector.get("change", 0)
            print(f"  {i}. {name}: {change_pct:+.2f}%")

    @pytest.mark.timeout(10)
    def test_sector_quote_structure_validation(self, auth_token):
        """
        验证 /stocks/sector/quote 返回数据的完整字段结构
        """
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        response = requests.get(f"{BASE_URL}/stocks/sector/quote", **kwargs)
        data = check_response(response)

        assert isinstance(data, dict)
        
        # 验证顶层字段
        assert "status" in data
        assert "date" in data
        assert "data" in data

        # 如果有数据，验证每个板块的字段
        if data["data"] and len(data["data"]) > 0:
            first_sector = data["data"][0]
            assert isinstance(first_sector, dict)
            
            # 验证常见字段（根据实际API返回可能有所不同）
            expected_fields = ["name"]
            for field in expected_fields:
                assert field in first_sector, f"板块数据缺少必需字段: {field}"

            print(f"\n板块数据结构示例:")
            print(f"  字段列表: {list(first_sector.keys())}")
