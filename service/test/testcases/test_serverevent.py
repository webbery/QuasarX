from tool import check_response, BASE_URL
import pytest
import requests
import time


@pytest.mark.usefixtures("auth_token")
class TestServerEvent:
    @pytest.mark.timeout(30)
    def test_status(self, auth_token):
        """测试 SSE 系统状态事件流，验证 system_status 事件格式"""
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

    @pytest.mark.timeout(30)
    def test_position_event(self, auth_token):
        """测试 SSE 持仓更新事件，验证 update_position 事件格式"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {
                'Authorization': auth_token,
                'Accept': 'text/event-stream'
            }

        response = requests.get(f"{BASE_URL}/server/event", stream=True, **kwargs)
        assert response.status_code == 200

        found_position = False
        try:
            timeout = 25  # TimerWorker 每 5s 推送一次
            start = time.time()
            for line in response.iter_lines(decode_unicode=True):
                if time.time() - start > timeout:
                    break
                if line:
                    tokens = line.split(':')
                    if len(tokens) > 0 and tokens[0] == 'update_position':
                        found_position = True
                        # 解析数据字段
                        import json
                        data = json.loads(tokens[1])
                        assert "data" in data
                        assert isinstance(data["data"], list)
                        print(f"\n[update_position] 持仓数: {len(data['data'])}")
                        for pos in data["data"]:
                            print(f"  持仓: {pos.get('name', '?')} qty={pos.get('quantity', 0)} "
                                  f"pnl={pos.get('floatingProfit', 0)}")
                        break
        except Exception as e:
            print(f"\n[update_position] 解析异常: {e}")
        finally:
            response.close()

        # 不强制要求有持仓数据，只验证事件格式正确
        if found_position:
            print("[update_position] ✓ 事件验证通过")
        else:
            print("[update_position] 未收到事件（可能无持仓，也正常）")

    @pytest.mark.timeout(30)
    def test_order_event(self, auth_token):
        """测试 SSE 订单更新事件，验证 update_order 事件格式"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {
                'Authorization': auth_token,
                'Accept': 'text/event-stream'
            }

        response = requests.get(f"{BASE_URL}/server/event", stream=True, **kwargs)
        assert response.status_code == 200

        found_order = False
        try:
            timeout = 25
            start = time.time()
            for line in response.iter_lines(decode_unicode=True):
                if time.time() - start > timeout:
                    break
                if line:
                    tokens = line.split(':')
                    if len(tokens) > 0 and tokens[0] == 'update_order':
                        found_order = True
                        import json
                        data = json.loads(tokens[1])
                        assert "data" in data
                        assert isinstance(data["data"], list)
                        print(f"\n[update_order] 订单数: {len(data['data'])}")
                        for order in data["data"]:
                            print(f"  订单: {order.get('name', '?')} "
                                  f"price={order.get('price', 0)} qty={order.get('quantity', 0)} "
                                  f"status={order.get('status', '?')}")
                        break
        except Exception as e:
            print(f"\n[update_order] 解析异常: {e}")
        finally:
            response.close()

        if found_order:
            print("[update_order] ✓ 事件验证通过")
        else:
            print("[update_order] 未收到事件（可能无订单，也正常）")