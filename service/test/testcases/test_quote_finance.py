import requests
import pytest
from tool import check_response, BASE_URL


@pytest.mark.usefixtures("auth_token")
class TestQuoteRead:
    """行情数据只读测试：列出表、查询数据"""

    def _kwargs(self, auth_token):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return kwargs

    @pytest.mark.timeout(10)
    def test_list_quote_tables(self, auth_token):
        """GET /v0/quote 无参数 → 列出所有行情表"""
        response = requests.get(f"{BASE_URL}/quote", **self._kwargs(auth_token))
        data = check_response(response)
        assert isinstance(data, list)
        for tbl in data:
            assert 'table' in tbl
            assert 'symbol_count' in tbl
            assert 'symbols' in tbl

    @pytest.mark.timeout(10)
    def test_query_quote_stock_data(self, auth_token):
        """GET /v0/quote?table=stock_1d&symbol=... → 查询股票行情明细"""
        kwargs = self._kwargs(auth_token)
        params = {'table': 'stock_1d', 'symbol': 'sh.600000'}
        response = requests.get(f"{BASE_URL}/quote", params=params, **kwargs)
        data = check_response(response)
        assert isinstance(data, dict)
        assert 'count' in data
        assert 'data' in data
        assert data['table'] == 'stock_1d'
        assert data['symbol'] == 'sh.600000'
        if data['count'] > 0:
            row = data['data'][0]
            assert 'datetime' in row
            assert 'open' in row
            assert 'close' in row
            assert 'high' in row
            assert 'low' in row
            assert 'volume' in row

    @pytest.mark.timeout(10)
    def test_query_quote_with_time_range(self, auth_token):
        """GET /v0/quote 带 start/end/limit 参数"""
        kwargs = self._kwargs(auth_token)
        params = {
            'table': 'stock_1d',
            'symbol': 'sh.600000',
            'start': '2024-01-01',
            'end': '2024-12-31',
            'limit': 10
        }
        response = requests.get(f"{BASE_URL}/quote", params=params, **kwargs)
        data = check_response(response)
        assert isinstance(data, dict)
        assert data['count'] <= 10


@pytest.mark.usefixtures("auth_token")
class TestQuoteDataLifecycle:
    """行情数据导入 → 导出 → 删除 生命周期测试（顺序依赖）"""

    TEST_SYMBOL = 'sh.999999'
    TEST_TABLE = 'stock_1d'

    # CSV: header + 3 行数据 (datetime,open,close,high,low,volume,turnover)
    # datetime 格式必须包含时间部分（TIMESTAMP 要求）
    TEST_CSV_LINES = [
        "datetime,open,close,high,low,volume,turnover",
        "2024-01-02 00:00:00,10.00,10.50,10.60,9.90,100000,1050000",
        "2024-01-03 00:00:00,10.50,10.80,10.90,10.40,120000,1296000",
        "2024-01-04 00:00:00,10.80,10.30,11.00,10.20,150000,1545000",
    ]

    def _kwargs(self, auth_token):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return kwargs

    # ---- Step 1: 导入 ----

    @pytest.mark.timeout(10)
    def test_1_import_quote_hfq(self, auth_token):
        """POST /v0/quote/data action=import 后复权"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'import',
            'table': self.TEST_TABLE,
            'symbol': self.TEST_SYMBOL,
            'adj': 'hfq',
            'data': self.TEST_CSV_LINES
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        data = check_response(response)
        assert data['message'] == 'Import successful'
        assert data['table'] == self.TEST_TABLE
        assert data['symbol'] == self.TEST_SYMBOL
        assert data['imported_rows'] == 3

    @pytest.mark.timeout(10)
    def test_2_import_quote_none(self, auth_token):
        """POST /v0/quote/data action=import 不复权"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'import',
            'table': self.TEST_TABLE,
            'symbol': self.TEST_SYMBOL,
            'adj': 'none',
            'data': self.TEST_CSV_LINES
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        data = check_response(response)
        assert data['imported_rows'] == 3

    # ---- Step 2: 验证导入数据可查询 ----

    @pytest.mark.timeout(10)
    def test_3_query_imported_data(self, auth_token):
        """GET /v0/quote 查询刚导入的测试数据"""
        kwargs = self._kwargs(auth_token)
        params = {'table': self.TEST_TABLE, 'symbol': self.TEST_SYMBOL}
        response = requests.get(f"{BASE_URL}/quote", params=params, **kwargs)
        data = check_response(response)
        assert data['count'] == 3
        assert data['data'][0]['open'] == 10.0

    # ---- Step 3: 导出 ----

    @pytest.mark.timeout(10)
    def test_4_export_quote_json(self, auth_token):
        """POST /v0/quote/data action=export format=json"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'export',
            'table': self.TEST_TABLE,
            'symbol': self.TEST_SYMBOL,
            'format': 'json'
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        data = check_response(response)
        assert data['table'] == self.TEST_TABLE
        assert data['symbol'] == self.TEST_SYMBOL
        assert 'data' in data
        assert isinstance(data['data'], str)
        assert 'date' in data['data']

    @pytest.mark.timeout(10)
    def test_5_export_quote_csv(self, auth_token):
        """POST /v0/quote/data action=export format=csv → text/csv"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'export',
            'table': self.TEST_TABLE,
            'symbol': self.TEST_SYMBOL,
            'format': 'csv'
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        assert response.status_code == 200
        assert 'text/csv' in response.headers.get('Content-Type', '')
        lines = response.text.strip().split('\n')
        assert len(lines) >= 2  # header + data

    @pytest.mark.timeout(10)
    def test_6_export_quote_with_time_range(self, auth_token):
        """POST /v0/quote/data export 带时间范围"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'export',
            'table': self.TEST_TABLE,
            'symbol': self.TEST_SYMBOL,
            'format': 'json',
            'start_time': '2024-01-02',
            'end_time': '2024-01-03'
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        data = check_response(response)
        assert 'data' in data

    # ---- Step 4: 删除 ----

    @pytest.mark.timeout(10)
    def test_7_delete_quote_symbol(self, auth_token):
        """DELETE /v0/quote?table=...&symbol=... → 删除指定标的"""
        kwargs = self._kwargs(auth_token)
        params = {'table': self.TEST_TABLE, 'symbol': self.TEST_SYMBOL}
        response = requests.delete(f"{BASE_URL}/quote", params=params, **kwargs)
        data = check_response(response)
        assert 'message' in data
        assert self.TEST_SYMBOL in data['message']

    @pytest.mark.timeout(10)
    def test_8_verify_deleted(self, auth_token):
        """DELETE 后查询确认数据已清除"""
        kwargs = self._kwargs(auth_token)
        params = {'table': self.TEST_TABLE, 'symbol': self.TEST_SYMBOL}
        response = requests.get(f"{BASE_URL}/quote", params=params, **kwargs)
        data = check_response(response)
        assert data['count'] == 0

    # ---- 错误处理 ----

    @pytest.mark.timeout(10)
    def test_import_missing_data(self, auth_token):
        """import 缺少 data 字段 → 400"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'import',
            'table': self.TEST_TABLE,
            'symbol': self.TEST_SYMBOL,
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        assert response.status_code == 400

    @pytest.mark.timeout(10)
    def test_import_missing_table(self, auth_token):
        """import 缺少 table 字段 → 400"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'import',
            'symbol': self.TEST_SYMBOL,
            'data': self.TEST_CSV_LINES,
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        assert response.status_code == 400

    @pytest.mark.timeout(10)
    def test_export_nonexistent_symbol(self, auth_token):
        """export 不存在的标的 → 500 + error"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'export',
            'table': self.TEST_TABLE,
            'symbol': 'sh.888888',
            'format': 'json'
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        assert response.status_code == 500
        body = response.json()
        assert 'error' in body
        assert 'No data found' in body['error']

    @pytest.mark.timeout(10)
    def test_invalid_action(self, auth_token):
        """无效 action → 400"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'invalid',
            'table': self.TEST_TABLE,
            'symbol': self.TEST_SYMBOL,
        }
        response = requests.post(f"{BASE_URL}/quote/data", **kwargs)
        assert response.status_code == 400


@pytest.mark.usefixtures("auth_token")
class TestQuoteCleanup:
    """DELETE /v0/quote/data 清理接口测试"""

    def _kwargs(self, auth_token):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return kwargs

    @pytest.mark.timeout(10)
    def test_cleanup_by_symbol(self, auth_token):
        """DELETE /v0/quote/data cleanup 指定标的"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'cleanup',
            'table': 'stock_1d',
            'symbol': 'sh.999999',
        }
        response = requests.delete(f"{BASE_URL}/quote/data", **kwargs)
        data = check_response(response)
        assert 'message' in data


@pytest.mark.usefixtures("auth_token")
class TestFinanceRead:
    """财务数据只读测试：列出表、查询数据"""

    def _kwargs(self, auth_token):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return kwargs

    @pytest.mark.timeout(10)
    def test_list_finance_tables(self, auth_token):
        """GET /v0/finance 无参数 → 列出所有财务类别表"""
        response = requests.get(f"{BASE_URL}/finance", **self._kwargs(auth_token))
        data = check_response(response)
        assert isinstance(data, dict)
        assert 'tables' in data
        assert isinstance(data['tables'], list)
        for tbl in data['tables']:
            assert 'category' in tbl
            assert 'name' in tbl
            assert 'symbols' in tbl

    @pytest.mark.timeout(10)
    def test_list_finance_category_symbols(self, auth_token):
        """GET /v0/finance?category=profit → 列出该类别下的标的"""
        kwargs = self._kwargs(auth_token)
        params = {'category': 'profit'}
        response = requests.get(f"{BASE_URL}/finance", params=params, **kwargs)
        data = check_response(response)
        assert isinstance(data, dict)
        assert data['category'] == 'profit'
        assert 'symbols' in data
        assert 'name' in data


@pytest.mark.usefixtures("auth_token")
class TestFinanceDataLifecycle:
    """财务数据导入 → 查询 → 导出 → 删除 生命周期测试（顺序依赖）"""

    TEST_CODE = 'sh.600000'
    TEST_CATEGORY = 'profit'

    # CSV: header + 2 行数据（列名必须用 camelCase 匹配 FinanceDB field_map）
    TEST_CSV_LINES = [
        "code,statDate,pubDate,roeAvg,npMargin,gpMargin,netProfit,epsTTM,MBRevenue,totalShare,liqaShare",
        "sh.600000,2024-03-31,2024-04-25,0.05,0.10,0.30,1000000,1.50,1.20,10000000,8000000",
        "sh.600000,2024-06-30,2024-08-25,0.06,0.11,0.31,1200000,1.80,1.25,10000000,8000000",
    ]

    def _kwargs(self, auth_token):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return kwargs

    # ---- Step 1: 导入 ----

    @pytest.mark.timeout(10)
    def test_1_import_finance(self, auth_token):
        """POST /v0/finance/data action=import"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'import',
            'category': self.TEST_CATEGORY,
            'code': self.TEST_CODE,
            'data': self.TEST_CSV_LINES,
        }
        response = requests.post(f"{BASE_URL}/finance/data", **kwargs)
        data = check_response(response)
        assert data['message'] == 'Import successful'
        assert data['category'] == self.TEST_CATEGORY
        assert data['imported_rows'] == 2

    # ---- Step 2: 查询验证 ----

    @pytest.mark.timeout(10)
    def test_2_query_imported_finance(self, auth_token):
        """GET /v0/finance?category=profit&code=... → 查询导入的财务数据"""
        kwargs = self._kwargs(auth_token)
        params = {'category': self.TEST_CATEGORY, 'code': self.TEST_CODE}
        response = requests.get(f"{BASE_URL}/finance", params=params, **kwargs)
        data = check_response(response)
        assert data['category'] == self.TEST_CATEGORY
        assert data['count'] == 2
        assert len(data['data']) == 2

    # ---- Step 3: 导出 ----

    @pytest.mark.timeout(10)
    def test_3_export_finance_json(self, auth_token):
        """POST /v0/finance/data action=export format=json"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'export',
            'category': self.TEST_CATEGORY,
            'code': self.TEST_CODE,
            'format': 'json'
        }
        response = requests.post(f"{BASE_URL}/finance/data", **kwargs)
        data = check_response(response)
        assert data['category'] == self.TEST_CATEGORY
        assert 'data' in data

    @pytest.mark.timeout(10)
    def test_4_export_finance_csv(self, auth_token):
        """POST /v0/finance/data action=export format=csv → text/csv"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'export',
            'category': self.TEST_CATEGORY,
            'code': self.TEST_CODE,
            'format': 'csv'
        }
        response = requests.post(f"{BASE_URL}/finance/data", **kwargs)
        assert response.status_code == 200
        assert 'text/csv' in response.headers.get('Content-Type', '')
        lines = response.text.strip().split('\n')
        assert len(lines) >= 2

    @pytest.mark.timeout(10)
    def test_5_export_finance_with_date_range(self, auth_token):
        """POST /v0/finance/data export 带日期范围"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'export',
            'category': self.TEST_CATEGORY,
            'code': self.TEST_CODE,
            'format': 'json',
            'start_date': '2024-01-01',
            'end_date': '2024-06-30'
        }
        response = requests.post(f"{BASE_URL}/finance/data", **kwargs)
        data = check_response(response)
        assert 'data' in data

    # ---- Step 4: 删除 ----

    @pytest.mark.timeout(10)
    def test_6_delete_finance_by_category(self, auth_token):
        """DELETE /v0/finance?category=profit&code=... → 删除指定类别"""
        kwargs = self._kwargs(auth_token)
        params = {'category': self.TEST_CATEGORY, 'code': self.TEST_CODE}
        response = requests.delete(f"{BASE_URL}/finance", params=params, **kwargs)
        data = check_response(response)
        assert data['code'] == self.TEST_CODE
        assert data['category'] == self.TEST_CATEGORY
        assert data['success'] is True

    @pytest.mark.timeout(10)
    def test_7_verify_deleted(self, auth_token):
        """DELETE 后查询确认数据已清除"""
        kwargs = self._kwargs(auth_token)
        params = {'category': self.TEST_CATEGORY, 'code': self.TEST_CODE}
        response = requests.get(f"{BASE_URL}/finance", params=params, **kwargs)
        data = check_response(response)
        assert data['count'] == 0

    # ---- 错误处理 ----

    @pytest.mark.timeout(10)
    def test_import_finance_missing_category(self, auth_token):
        """import 缺少 category → 400"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'import',
            'data': self.TEST_CSV_LINES,
        }
        response = requests.post(f"{BASE_URL}/finance/data", **kwargs)
        assert response.status_code == 400

    @pytest.mark.timeout(10)
    def test_import_finance_missing_data(self, auth_token):
        """import 缺少 data → 400"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'import',
            'category': self.TEST_CATEGORY,
        }
        response = requests.post(f"{BASE_URL}/finance/data", **kwargs)
        assert response.status_code == 400

    @pytest.mark.timeout(10)
    def test_delete_finance_missing_code(self, auth_token):
        """DELETE /v0/finance 缺少 code → 400"""
        kwargs = self._kwargs(auth_token)
        response = requests.delete(f"{BASE_URL}/finance", **kwargs)
        assert response.status_code == 400


@pytest.mark.usefixtures("auth_token")
class TestFinanceCleanup:
    """DELETE /v0/finance/data 清理接口测试"""

    def _kwargs(self, auth_token):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return kwargs

    @pytest.mark.timeout(10)
    def test_cleanup_finance_by_code(self, auth_token):
        """DELETE /v0/finance/data cleanup 指定标的"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'action': 'cleanup',
            'category': 'profit',
            'code': 'sh.600000',
        }
        response = requests.delete(f"{BASE_URL}/finance/data", **kwargs)
        data = check_response(response)
        assert 'message' in data

    @pytest.mark.timeout(10)
    def test_delete_finance_all_categories(self, auth_token):
        """DELETE /v0/finance?code=... 不指定 category → 删除所有类别"""
        kwargs = self._kwargs(auth_token)
        params = {'code': 'sh.600000'}
        response = requests.delete(f"{BASE_URL}/finance", params=params, **kwargs)
        data = check_response(response)
        assert data['code'] == 'sh.600000'
        assert 'deleted_tables' in data


@pytest.mark.usefixtures("auth_token")
class TestDownloadEndpoints:
    """下载端点请求验证（只验证 API 契约，不等待后台下载完成）"""

    def _kwargs(self, auth_token):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return kwargs

    # ---- Quote Download ----

    @pytest.mark.timeout(10)
    def test_quote_download_valid(self, auth_token):
        """POST /v0/quote 合法请求 → 返回 started + groups"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'symbols': '600000.SH',
            'freq': 'daily',
        }
        response = requests.post(f"{BASE_URL}/quote", **kwargs)
        data = check_response(response)
        assert data['status'] == 'started'
        assert 'groups' in data
        assert isinstance(data['groups'], list)
        assert len(data['groups']) > 0
        grp = data['groups'][0]
        assert 'asset_type' in grp
        assert 'table' in grp
        assert 'symbols' in grp

    @pytest.mark.timeout(10)
    def test_quote_download_missing_symbols(self, auth_token):
        """POST /v0/quote 缺少 symbols → 400"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {'freq': 'daily'}
        response = requests.post(f"{BASE_URL}/quote", **kwargs)
        assert response.status_code == 400

    @pytest.mark.timeout(10)
    def test_quote_download_invalid_json(self, auth_token):
        """POST /v0/quote 非法 JSON → 400"""
        kwargs = self._kwargs(auth_token)
        kwargs['data'] = 'not json'
        kwargs['headers'] = {**kwargs.get('headers', {}), 'Content-Type': 'application/json'}
        response = requests.post(f"{BASE_URL}/quote", **kwargs)
        assert response.status_code == 400

    # ---- Finance Download ----

    @pytest.mark.timeout(10)
    def test_finance_download_valid(self, auth_token):
        """POST /v0/finance 合法请求 → 返回 started"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'code': '600519.SH',
            'category': 'profit',
        }
        response = requests.post(f"{BASE_URL}/finance", **kwargs)
        data = check_response(response)
        assert data['status'] == 'started'
        assert data['code'] == '600519.SH'
        assert data['category'] == 'profit'

    @pytest.mark.timeout(10)
    def test_finance_download_missing_code(self, auth_token):
        """POST /v0/finance 缺少 code → 400"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {'category': 'profit'}
        response = requests.post(f"{BASE_URL}/finance", **kwargs)
        assert response.status_code == 400

    @pytest.mark.timeout(10)
    def test_finance_download_invalid_category(self, auth_token):
        """POST /v0/finance 无效 category → 400"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {'code': '600519.SH', 'category': 'invalid'}
        response = requests.post(f"{BASE_URL}/finance", **kwargs)
        assert response.status_code == 400

    @pytest.mark.timeout(10)
    def test_finance_download_all_categories(self, auth_token):
        """POST /v0/finance category=all → 下载全部类别"""
        kwargs = self._kwargs(auth_token)
        kwargs['json'] = {
            'code': '600519.SH',
            'category': 'all',
        }
        response = requests.post(f"{BASE_URL}/finance", **kwargs)
        data = check_response(response)
        assert data['status'] == 'started'
        assert data['category'] == 'all'
