<template>
    <div class="position-manager">
        <!-- 系统监控面板 -->
        <div class="card system-monitor">
          <div class="card-header">
            <h2>系统监控</h2>
          </div>
          <div class="card-content">
            <div class="form-row">
              <div class="form-group">
                <label>交易权限</label>
                <select class="form-control" v-model="tradePermission">
                  <option value="full">全权限</option>
                  <option value="readonly">只读</option>
                  <option value="disabled">禁止交易</option>
                </select>
              </div>
              <div class="form-group">
                <label>交易阈值</label>
                <input type="text" class="form-control" v-model="tradeThreshold" placeholder="请输入阈值">
              </div>
            </div>
            
            <div class="warning-banner" v-if="abnormalTradeDetected">
              检测到异常交易行为，已自动限制交易
            </div>
            
            <div class="form-row">
              <button class="btn btn-warning" @click="handleGlobalCancel">一键撤单</button>
              <button class="btn btn-warning" @click="handleEmergency">应急处置</button>
              <button class="btn btn-warning" @click="handleErrorReport">错误报告</button>
              <button class="btn btn-danger" @click="handleEmergencyStop">紧急停止</button>
            </div>
          </div>
        </div>

        <!-- 市场持仓面板 -->
        <div class="card market-positions">
          <div class="card-header">
            <h2>持仓管理</h2>
            <span>
              <span v-if="activeMarketTab === 'stock'">股票</span>
              <span v-else-if="activeMarketTab === 'option'">期权</span>
              <span v-else-if="activeMarketTab === 'future'">期货</span>
              总盈亏: {{  }}</span>
            <div class="tab-navigation">
              <div 
                class="tab-item" 
                :class="{active: activeMarketTab === 'stock'}" 
                @click="activeMarketTab = 'stock'"
              >
                股票持仓
              </div>
              <div 
                class="tab-item" 
                :class="{active: activeMarketTab === 'option'}" 
                @click="activeMarketTab = 'option'"
              >
                期权持仓
              </div>
              <div 
                class="tab-item" 
                :class="{active: activeMarketTab === 'future'}" 
                @click="activeMarketTab = 'future'"
              >
                期货持仓
              </div>
            </div>
          </div>
          
          <div class="card-content">
            <!-- 股票持仓 -->
            <div class="tab-pane" :class="{active: activeMarketTab === 'stock'}">              
              <table class="position-table">
                <thead>
                  <tr>
                    <th>代码</th>
                    <th>交易所</th>
                    <th>名称</th>
                    <th>持仓数量</th>
                    <th>可用数量</th>
                    <th>成本价</th>
                    <th>当前价</th>
                    <th>浮动盈亏</th>
                    <th>操作</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="position in stockPositions" :key="position.code">
                    <td>{{ position.code }}</td>
                    <td>{{ position.exchange }}</td>
                    <td>{{ position.name }}</td>
                    <td>{{ position.quantity }}</td>
                    <td>{{ position.available }}</td>
                    <td>{{ position.costPrice }}</td>
                    <td>{{ position.currentPrice }}</td>
                    <td :class="position.profit > 0 ? 'profit-positive' : 'profit-negative'">
                      {{ position.profit > 0 ? '+' : '' }}{{ position.profit }}
                    </td>
                    <td>
                      <button class="btn btn-mini" @click="openStockOperation(position, 'buy')">买入</button>
                      <button class="btn btn-mini" @click="openStockOperation(position, 'buy_margin')">融资买入</button>
                      <button class="btn btn-mini btn-danger" @click="openStockOperation(position, 'sell')">卖出</button>
                      <button class="btn btn-mini btn-danger" @click="openStockOperation(position, 'sell_short')">融券卖出</button>
                    </td>
                  </tr>
                </tbody>
              </table>
              
              <!-- 股票委托列表 -->
              <div class="order-list">
                <div class="pane-header">
                  <h3>股票委托</h3>
                  <button class="btn btn-warning" @click="handleStockCancel">一键撤单</button>
                </div>
                
                <table class="order-table">
                  <thead>
                    <tr>
                      <th>代码</th>
                      <th>名称</th>
                      <th>委托类型</th>
                      <th>订单类型</th>
                      <th>价格</th>
                      <th>数量</th>
                      <th>状态</th>
                      <th>委托时间</th>
                      <th>操作</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr v-for="order in stockOrders" :key="order.id" class="order-row">
                      <td>{{ order.code }}</td>
                      <td>{{ order.name }}</td>
                      <td :class="getOrderTypeClass(order.type)">{{ getOrderTypeText(order.type) }}</td>
                      <td>
                        <span class="order-type-badge" :class="`order-type-${order.orderType}`">
                          {{ getOrderTypeDisplayText(order.orderType) }}
                        </span>
                      </td>
                      <td>{{ order.price }}</td>
                      <td>{{ order.quantity }}</td>
                      <td>
                        <span class="order-status" :class="`status-${order.status}`">
                          {{ getOrderStatusText(order.status) }}
                        </span>
                      </td>
                      <td>{{ order.orderTime || formatTime(order.timestamp) }}</td>
                      <td>
                        <button 
                          class="btn btn-mini btn-danger" 
                          @click="cancelOrder(order)" 
                          v-if="order.status === 'pending' || order.status === 'waiting'"
                        >
                          撤单
                        </button>
                        <span v-else class="no-action">-</span>
                      </td>
                    </tr>
                  </tbody>
                </table>
                <div class="empty-state" v-if="stockOrders.length === 0">
                  暂无委托记录
                </div>
              </div>
            </div>
            
            <!-- 期权持仓 -->
            <div class="tab-pane" :class="{active: activeMarketTab === 'option'}">
              <div class="tab-container">
                <div class="tab-header">
                  <div class="tab-item" :class="{active: activeOptionTab === 'positions'}" @click="activeOptionTab = 'positions'">持仓</div>
                  <div class="tab-item" :class="{active: activeOptionTab === 'settings'}" @click="activeOptionTab = 'settings'">参数设置</div>
                </div>
                <div class="tab-content">
                  <div class="tab-pane" :class="{active: activeOptionTab === 'positions'}">
                    <table class="position-table">
                      <thead>
                        <tr>
                          <th>代码</th>
                          <th>名称</th>
                          <th>持仓数量</th>
                          <th>可用数量</th>
                          <th>成本价</th>
                          <th>当前价</th>
                          <th>浮动盈亏</th>
                          <th>操作</th>
                        </tr>
                      </thead>
                      <tbody>
                        <tr v-for="position in optionPositions" :key="position.code">
                          <td>{{ position.code }}</td>
                          <td>{{ position.name }}</td>
                          <td>{{ position.quantity }}</td>
                          <td>{{ position.available }}</td>
                          <td>{{ position.costPrice }}</td>
                          <td>{{ position.currentPrice }}</td>
                          <td :class="position.profit > 0 ? 'profit-positive' : 'profit-negative'">
                            {{ position.profit > 0 ? '+' : '' }}{{ position.profit }}
                          </td>
                          <td>
                            <button class="btn btn-mini" @click="openOptionOperation(position, 'buyOpen')">买开</button>
                            <button class="btn btn-mini" @click="openOptionOperation(position, 'sellOpen')">卖开</button>
                            <button class="btn btn-mini btn-danger" @click="openOptionOperation(position, 'buyClose')">买平</button>
                            <button class="btn btn-mini btn-danger" @click="openOptionOperation(position, 'sellClose')">卖平</button>
                          </td>
                        </tr>
                      </tbody>
                    </table>
                  </div>
                  <div class="tab-pane" :class="{active: activeOptionTab === 'settings'}">
                    <div class="form-row">
                      <div class="form-group">
                        <label>成交持仓比例控制</label>
                        <input type="text" class="form-control" v-model="optionSettings.positionRatio" placeholder="请输入比例">
                      </div>
                      <div class="form-group">
                        <label>程序化流速控制</label>
                        <input type="text" class="form-control" v-model="optionSettings.speedLimit" placeholder="请输入流速">
                      </div>
                    </div>
                    <div class="form-row">
                      <div class="form-group">
                        <div class="checkbox-group">
                          <input type="checkbox" v-model="optionSettings.selfTradeCheck" id="selfTradeCheck">
                          <label for="selfTradeCheck">自成交判断</label>
                        </div>
                      </div>
                    </div>
                    <button class="btn btn-primary" @click="saveOptionSettings">保存设置</button>
                  </div>
                </div>
              </div>
              
              <!-- 期权委托列表 -->
              <div class="order-list">
                <h3>期权委托</h3>
                <div class="order-item" v-for="order in optionOrders" :key="order.id">
                  <div>
                    <span>{{ order.code }} {{ order.name }}</span> | 
                    <span>{{ order.typeText }}</span> | 
                    <span>{{ order.price }} × {{ order.quantity }}</span>
                  </div>
                  <div>
                    <span class="order-status" :class="`status-${order.status}`">
                      {{ order.status === 'pending' ? '待成交' : order.status === 'filled' ? '已成交' : '已撤单' }}
                    </span>
                    <button class="btn btn-mini" @click="cancelOrder(order)" v-if="order.status === 'pending'">撤单</button>
                  </div>
                </div>
              </div>
            </div>
            
            <!-- 期货持仓 -->
            <div class="tab-pane" :class="{active: activeMarketTab === 'future'}">
              <table class="position-table">
                <thead>
                  <tr>
                    <th>合约</th>
                    <th>名称</th>
                    <th>多空</th>
                    <th>持仓数量</th>
                    <th>可用数量</th>
                    <th>开仓均价</th>
                    <th>当前价</th>
                    <th>浮动盈亏</th>
                    <th>操作</th>
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="position in futurePositions" :key="position.code" :class="position.direction === 'long' ? 'long-position' : 'short-position'">
                    <td>{{ position.code }}</td>
                    <td>{{ position.name }}</td>
                    <td :class="position.direction === 'long' ? 'direction-long' : 'direction-short'">
                      {{ position.direction === 'long' ? '多头' : '空头' }}
                    </td>
                    <td>{{ position.quantity }}</td>
                    <td>{{ position.available }}</td>
                    <td>{{ position.openPrice }}</td>
                    <td>{{ position.currentPrice }}</td>
                    <td :class="position.profit > 0 ? 'profit-positive' : 'profit-negative'">
                      {{ position.profit > 0 ? '+' : '' }}{{ position.profit }}
                    </td>
                    <td>
                      <button class="btn btn-mini" @click="openFutureOperation(position, 'open')">开仓</button>
                      <button class="btn btn-mini btn-danger" @click="openFutureOperation(position, 'close')">平仓</button>
                    </td>
                  </tr>
                </tbody>
              </table>
              
              <!-- 期货委托列表 -->
              <div class="order-list">
                <h3>期货委托</h3>
                <div class="order-item" v-for="order in futureOrders" :key="order.id">
                  <div>
                    <span>{{ order.code }} {{ order.name }}</span> | 
                    <span>{{ order.type === 'open' ? '开仓' : '平仓' }}</span> | 
                    <span :class="order.direction === 'long' ? 'direction-long' : 'direction-short'">{{ order.direction === 'long' ? '多头' : '空头' }}</span> | 
                    <span>{{ order.price }} × {{ order.quantity }}</span>
                  </div>
                  <div>
                    <span class="order-status" :class="`status-${order.status}`">
                      {{ order.status === 'pending' ? '待成交' : order.status === 'filled' ? '已成交' : '已撤单' }}
                    </span>
                    <button class="btn btn-mini" @click="cancelOrder(order)" v-if="order.status === 'pending'">撤单</button>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>

        <!-- 模态框 -->
        <div class="modal" v-if="showModal">
          <div class="modal-content">
            <div class="modal-header">
              <h3>{{ modalTitle }}</h3>
              <span class="close" @click="closeModal">&times;</span>
            </div>
            <p>{{ modalMessage }}</p>
            <div class="modal-footer">
              <button class="btn" @click="closeModal">取消</button>
              <button class="btn btn-primary" @click="confirmModal">确定</button>
            </div>
          </div>
        </div>
    </div>
</template>

<script setup>
import axios from 'axios';
import { ref, computed, onMounted, reactive, onUnmounted } from 'vue'
import sseService from '@/ts/SSEService';
import { message } from '@/tool';

const STOCK_ORDER = 'stockOrders'
// 市场选项卡
const activeMarketTab = ref('stock');

// 股票相关数据
const stockPositions = ref([]);
const selectedStock = ref(null);
const stockOperationType = ref('');
const capitalChecked = ref(false);
const stockChecked = ref(false);
const newStockOperation = ref(false);

const stockOrder = reactive({
    orderType: 'limit', // 默认限价单
    price: '',
    quantity: '',
    conditionType: 'price_ge', // 条件单类型
    triggerPrice: '', // 触发价格
    stopPrice: '', // 止损价格
    validity: 'day', // 有效期
    validityDate: '' // 指定日期
});

const newStockOrder = reactive({
    code: '',
    name: '',
    tradeType: 'buy',
    orderType: 'limit', // 默认限价单
    price: '',
    curPrice: '',
    quantity: '',
    conditionType: 'price_ge', // 条件单类型
    triggerPrice: '', // 触发价格
    stopPrice: '', // 止损价格
    validity: 'day', // 有效期
    validityDate: '' // 指定日期
});

const stockOrders = ref([]);

// 期权相关数据
const optionPositions = ref([
    { code: '10002467', name: '50ETF购3月3500', quantity: 10, available: 10, costPrice: 0.0850, currentPrice: 0.0920, profit: 70 },
    { code: '10002468', name: '50ETF沽3月3500', quantity: 5, available: 5, costPrice: 0.0450, currentPrice: 0.0380, profit: -35 }
]);

const activeOptionTab = ref('positions');
const selectedOption = ref(null);
const optionOperationType = ref('');
const newOptionOperation = ref(false);

const optionOrder = reactive({
    price: '',
    quantity: ''
});

const newOptionOrder = reactive({
    code: '',
    name: '',
    type: 'buyOpen',
    price: '',
    quantity: ''
});

const optionOrders = ref([
    { id: 1, code: '10002467', name: '50ETF购3月3500', type: 'buyOpen', typeText: '买开', price: 0.0910, quantity: 5, status: 'pending' }
]);

const optionSettings = reactive({
    positionRatio: '0.8',
    speedLimit: '100',
    selfTradeCheck: true
});

// 期货相关数据
const futurePositions = ref([
    { code: 'IF2403', name: '沪深300指数期货2403', direction: 'long', quantity: 2, available: 2, openPrice: 3850.5, currentPrice: 3902.3, profit: 10360 },
    { code: 'IC2403', name: '中证500指数期货2403', direction: 'short', quantity: 1, available: 1, openPrice: 5820.0, currentPrice: 5756.8, profit: 6320 }
]);

const selectedFuture = ref(null);
const futureOperationType = ref('');
const newFutureOperation = ref(false);

const futureOrder = reactive({
    price: '',
    quantity: '',
    direction: 'long'
});

const newFutureOrder = reactive({
    code: '',
    name: '',
    direction: 'long',
    price: '',
    quantity: ''
});

const futureOrders = ref([
    { id: 1, code: 'IF2403', name: '沪深300指数期货2403', type: 'open', direction: 'long', price: 3895.0, quantity: 1, status: 'pending' }
]);

// 系统监控数据
const tradePermission = ref('full');
const tradeThreshold = ref('1000000');
const abnormalTradeDetected = ref(false);

// 模态框
const showModal = ref(false);
const modalTitle = ref('');
const modalMessage = ref('');
const modalCallback = ref(null);

// 计算属性
const canPlaceStockOrder = computed(() => {
    // 基础验证
    // if (stockOperationType.value === 'buy' || stockOperationType.value === 'buy_margin') {
    //     if (!capitalChecked.value) return false;
    // } else {
    //     if (!stockChecked.value) return false;
    // }
    
    // 根据订单类型进行验证
    switch(stockOrder.orderType) {
        case 'limit':
            return stockOrder.price && stockOrder.quantity;
        case 'market':
            return stockOrder.quantity;
        case 'conditional':
            return stockOrder.triggerPrice && stockOrder.price && stockOrder.quantity;
        case 'stop':
            return stockOrder.stopPrice && stockOrder.price && stockOrder.quantity;
        default:
            return false;
    }
});

const canPlaceNewStockOrder = computed(() => {
    // 融资买入需要验资
    if (newStockOrder.tradeType === 'buy_margin') {
        if (!capitalChecked.value) return false;
    }
    
    // 基础验证
    if (!newStockOrder.code) {
        message.show('股票代码不能为空')
        return false;
    }
    if (!newStockOrder.quantity) {
        message.show('股票数量不能为空')
        return false;
    }
    if (!newStockOrder.price > 0) {
      message.show('股票价格必须大于0')
        return false;
    }
    console.info(newStockOrder.orderType)
    // 根据订单类型进行验证
    switch(newStockOrder.orderType) {
        case 'limit':
        case 'market':
            return true;
        case 'conditional':
            return newStockOrder.triggerPrice && newStockOrder.price;
        case 'stop':
            return newStockOrder.stopPrice && newStockOrder.price;
        default:
            return false;
    }
});

const handleOrderTypeChange = (type) => {
    if (type === 'new') {
        // 重置新订单的相关字段
        newStockOrder.price = '';
        newStockOrder.triggerPrice = '';
        newStockOrder.stopPrice = '';
    } else {
        // 重置现有股票订单的相关字段
        stockOrder.price = '';
        stockOrder.triggerPrice = '';
        stockOrder.stopPrice = '';
    }
};

const stockOperationTypeText = computed(() => {
    switch(stockOperationType.value) {
        case 'buy': return '普通买入';
        case 'buy_margin': return '融资买入';
        case 'sell': return '普通卖出';
        case 'sell_short': return '融券卖出';
        default: return '';
    }
});

const optionOperationTypeText = computed(() => {
    switch(optionOperationType.value) {
        case 'buyOpen': return '限价买开';
        case 'sellOpen': return '限价卖开';
        case 'buyClose': return '限价买平';
        case 'sellClose': return '限价卖平';
        default: return '';
    }
});

const getOrderStatusText = (status) => {
    switch(status) {
        case 'pending': return '待成交';
        case 'filled': return '已成交';
        case 'cancelled': return '已撤单';
        case 'rejected': return '已失败';
        // case 'waiting': 
        //     return order.orderType === 'conditional' ? '等待触发' : 
        //            order.orderType === 'stop' ? '监控中' : '待成交';
        default: return '监控中';
    }
};

const getOrderStatusType = (type) => {
  let status = 'pending'
  switch (type) {
    case 1: status = 'pending'; break;
    case 2: status = 'rejected'; break;
    case 3: status = 'part_filled'; break;
    case 4: status = 'filled'; break;
    case 5: status = 'rejected'; break;
    case 6: status = 'cancel_part'; break;
    case 7: status = 'cancelled'; break;
    case 8: status = 'pending'; break;
    case 9: status = 'cancelled_filled'; break;
    default: break;
  }
  return status;
}

const getOrderTypeDisplayText = (orderType) => {
    switch(orderType) {
        case 'limit': return '限价';
        case 'market': return '市价';
        case 'conditional': return '条件';
        case 'stop': return '止损';
        default: return orderType;
    }
};

const getOrderType = (orgType) => {
  switch(orgType) {
    case 0: return 'market';
    case 1: return 'limit';
    case 2: return 'conditional';
    case 3: return 'stop';
    default: return 'limit';
  }
}
// 方法

const formatTime = (timestamp) => {
  if (!timestamp) return '-';
  const date = new Date(timestamp);
  return `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')} ${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}:${String(date.getSeconds()).padStart(2, '0')}`;
};

const openNewStockOperation = () => {
    newStockOperation.value = true;
    selectedStock.value = null;
    stockOperationType.value = '';
    capitalChecked.value = false;
    newStockOrder.code = '';
    newStockOrder.name = '';
    newStockOrder.tradeType = 'buy';
    newStockOrder.price = '';
    newStockOrder.quantity = '';
};

const closeNewStockOperation = () => {
    newStockOperation.value = false;
};

const openStockOperation = (stock, operationType) => {
    selectedStock.value = stock;
    stockOperationType.value = operationType;
    newStockOperation.value = false;
    capitalChecked.value = false;
    stockChecked.value = false;
    stockOrder.price = '';
    stockOrder.quantity = '';
};

const closeStockOperation = () => {
    selectedStock.value = null;
    stockOperationType.value = '';
};

const checkCapital = () => {
    openModal('验资确认', '确定要进行验资操作吗？', () => {
        capitalChecked.value = true;
        message.show('验资通过', 'success')
    });
};

const checkStock = () => {
    openModal('验券确认', '确定要进行验券操作吗？', () => {
        stockChecked.value = true;
        message.show('验券通过', 'success')
    });
};

const placeStockOrder = () => {
    if (canPlaceNewStockOrder) {
        return;
    }
    const order = {
        code: selectedStock.value.code,
        name: selectedStock.value.name,
        type: stockOperationType.value,
        orderType: stockOrder.orderType,
        price: stockOrder.price,
        quantity: stockOrder.quantity,
        conditionType: stockOrder.conditionType,
        triggerPrice: stockOrder.triggerPrice,
        stopPrice: stockOrder.stopPrice,
        validity: stockOrder.validity,
        validityDate: stockOrder.validityDate,
        status: getOrderStatusType(stockOrder.status)
    };
    console.info('stock commit', order)
    
    stockOrders.value.unshift(order);
    message.show('股票订单提交成功', 'success')
    closeStockOperation();
};

const placeNewStockOrder = async () => {
    let direct = 0;
    switch(newStockOrder.tradeType) {
      case 'buy': direct = 0; break;
      case 'sell': direct = 1; break;
      default: break;
    }
    let type = 0;
    switch (newStockOrder.orderType) {
      case 'limit': type = 1; break;  // 限价单
      case 'market': type = 0; break; // 市价单
      default: type = 0; break;
    }
    const order = {
        id: Date.now(),
        symbol: newStockOrder.code,
        name: newStockOrder.name,
        direct: direct,
        kind: 0,
        type: type,
        prices: [parseFloat(newStockOrder.price)],
        quantity: parseInt(newStockOrder.quantity),
        conditionType: newStockOrder.conditionType,
        triggerPrice: parseFloat(newStockOrder.triggerPrice),
        stopPrice: parseFloat(newStockOrder.stopPrice),
        validity: newStockOrder.validity,
        validityDate: newStockOrder.validityDate
    };
    console.info('buy:', order)
    try{
      const res = await axios.post('/v0/trade/order', order)
      console.info('trade order:', res)
      let displayOrder = {
        id: order.id, 
        code: order.symbol, 
        name: order.name, 
        type: (direct === 0? 'buy': 'sell'), 
        orderType: newStockOrder.orderType,
        price: newStockOrder.price, 
        quantity: newStockOrder.quantity, 
        status: 'pending',
        sysID: res['data'].sysID,
        timestamp: Date.now() // 添加时间戳
      };
      stockOrders.value.unshift(displayOrder);
      message.success('新代码买入订单提交成功');
      closeNewStockOperation();
    } catch (error) {
      console.info('error:')
      message.error('error:' + error)
    }
    
};

const openNewOptionOperation = () => {
    newOptionOperation.value = true;
    selectedOption.value = null;
    optionOperationType.value = '';
    newOptionOrder.code = '';
    newOptionOrder.name = '';
    newOptionOrder.type = 'buyOpen';
    newOptionOrder.price = '';
    newOptionOrder.quantity = '';
};

const closeNewOptionOperation = () => {
    newOptionOperation.value = false;
};

const openOptionOperation = (option, operationType) => {
    selectedOption.value = option;
    optionOperationType.value = operationType;
    newOptionOperation.value = false;
    optionOrder.price = '';
    optionOrder.quantity = '';
};

const closeOptionOperation = () => {
    selectedOption.value = null;
    optionOperationType.value = '';
};

const placeOptionOrder = () => {
    const order = {
        id: Date.now(),
        code: selectedOption.value.code,
        name: selectedOption.value.name,
        type: optionOperationType.value,
        typeText: optionOperationTypeText.value,
        price: optionOrder.price,
        quantity: optionOrder.quantity,
        status: 'pending'
    };
    
    optionOrders.value.unshift(order);
    message.show('期权订单提交成功', 'success')
    closeOptionOperation();
};

const placeNewOptionOrder = () => {
    const order = {
        id: Date.now(),
        code: newOptionOrder.code,
        name: newOptionOrder.name,
        type: newOptionOrder.type,
        typeText: newOptionOrder.type === 'buyOpen' ? '买开' : '卖开',
        price: newOptionOrder.price,
        quantity: newOptionOrder.quantity,
        status: 'pending'
    };
    
    optionOrders.value.unshift(order);
    message.show('新合约买入订单提交成功', 'success')
    closeNewOptionOperation();
};

const saveOptionSettings = () => {
    message.show('期权参数设置已保存', 'success')
};

const closeNewFutureOperation = () => {
    newFutureOperation.value = false;
};

const openFutureOperation = (future, operationType) => {
    selectedFuture.value = future;
    futureOperationType.value = operationType;
    newFutureOperation.value = false;
    futureOrder.price = '';
    futureOrder.quantity = '';
    futureOrder.direction = 'long';
};

const closeFutureOperation = () => {
    selectedFuture.value = null;
    futureOperationType.value = '';
};

const placeFutureOrder = () => {
    const order = {
        id: Date.now(),
        code: selectedFuture.value.code,
        name: selectedFuture.value.name,
        type: futureOperationType.value,
        direction: futureOrder.direction,
        price: futureOrder.price,
        quantity: futureOrder.quantity,
        status: 'pending'
    };
    
    futureOrders.value.unshift(order);
    message.show('期货订单提交成功', 'success')
    closeFutureOperation();
};

const placeNewFutureOrder = () => {
    const order = {
        id: Date.now(),
        code: newFutureOrder.code,
        name: newFutureOrder.name,
        type: 'open',
        direction: newFutureOrder.direction,
        price: newFutureOrder.price,
        quantity: newFutureOrder.quantity,
        status: 'pending'
    };
    
    futureOrders.value.unshift(order);
    message.show('新合约买入订单提交成功', 'success')
    closeNewFutureOperation();
};

const cancelOrder = (order) => {
    const info = order.name + ", 价格:" + order.price
    openModal('撤单确认', '确定要撤销此订单吗？' + info, async () => {
        console.info('cancel order:', order)
        const sysID = order.sysID
        const params = {
          id: sysID
        }
        const result = await axios.delete('/v0/trade/order', params)
        if (result.status != 200) {
          message.error('撤销订单失败')
        } else {
          message.success('撤销订单请求已发出')
        }
    });
};

const handleGlobalCancel = () => {
    openModal('一键撤单确认', '确定要撤销所有待成交订单吗？', () => {
        // 撤消所有待成交订单
        stockOrders.value.forEach(order => {
            if (order.status === 'pending') order.status = 'cancelled';
        });
        
        optionOrders.value.forEach(order => {
            if (order.status === 'pending') order.status = 'cancelled';
        });
        
        futureOrders.value.forEach(order => {
            if (order.status === 'pending') order.status = 'cancelled';
        });
        
        message.show('所有待成交订单已撤销', 'success')
    });
};

const handleEmergency = () => {
    openModal('应急处置确认', '确定要执行应急处置操作吗？此操作将暂停所有交易功能。', () => {
        tradePermission.value = 'disabled';
        message.show('应急处置已执行，交易功能已暂停', 'success')
    });
};

const handleErrorReport = () => {
    message.show('错误报告已生成，请联系技术支持', 'success')
};

const handleEmergencyStop = () => {
    openModal('紧急停止确认', '确定要紧急停止所有交易吗？此操作不可逆。', () => {
        tradePermission.value = 'disabled';
        abnormalTradeDetected.value = true;
        message.show('交易已紧急停止', 'success')
    });
};

// 获取订单类型文本和样式
const getOrderTypeText = (type) => {
    switch(type) {
        case 'buy': return '普通买入';
        case 'buy_margin': return '融资买入';
        case 'sell': return '普通卖出';
        case 'sell_short': return '融券卖出';
        default: return type;
    }
};

const getOrderTypeClass = (type) => {
    switch(type) {
        case 'buy':
        case 'buy_margin':
            return 'order-type-buy';
        case 'sell':
        case 'sell_short':
            return 'order-type-sell';
        default:
            return '';
    }
};

const getExchange = (id) => {
    const tokens = id.split('.')
    switch (tokens[0]) {
      case 'sz': return '深交所'
      case 'sh': return '上交所'
      case 'bj': return '北交所'
      default: return tokens[0]
    }
}
// 模态框方法
const openModal = (title, message, callback) => {
    modalTitle.value = title;
    modalMessage.value = message;
    modalCallback.value = callback;
    showModal.value = true;
};

const closeModal = () => {
    showModal.value = false;
    modalTitle.value = '';
    modalMessage.value = '';
    modalCallback.value = null;
};

const confirmModal = () => {
    if (modalCallback.value) {
        modalCallback.value();
    }
    closeModal();
};

const onOrderSuccess = (message) => {
  console.info('onOrderSuccess sse: ', message)
  
}

const onPositionUpdate = (message) => {
  console.info('onPositionUpdate sse: ', message)
  const data = message.data;
  let updated = false;
  let profit = (parseFloat(data.curPrice) - parseFloat(data.price)) * parseFloat(data.quantity)
  stockPositions.value.forEach((value) => {
    if (value.code == data.id) {
      updated = true;
      value.currentPrice = parseFloat(data.curPrice).toFixed(4);
      value.profit = parseFloat(profit).toFixed(4);
    }
  })
  if (updated === false) {
    const stock = {
      code: data.id,
      exchange: getExchange(data.id),
      name: data.name,
      quantity: data.quantity,
      costPrice: parseFloat(data.price).toFixed(4),
      currentPrice: parseFloat(data.curPrice).toFixed(4),
      available: data.valid_quantity,
      profit: parseFloat(profit).toFixed(4)
    }
    stockPositions.value.push(stock)
  }
}

const onOrderUpdate = (message) => {
  // 订单状态更新
  console.info('update order:', message.data)
  stockOrders.value = []
  const data = JSON.parse(message.data['data'])
  for (const item of data) {
    const order = {
        code: item["id"],
        name: item["name"],
        type: (item["direct"] === 0? 'buy': 'sell'),
        orderType: getOrderType(item['orderType']),
        price: item['price'],
        quantity: item['quantity'],
        // conditionType: stockOrder.conditionType,
        // triggerPrice: stockOrder.triggerPrice,
        // stopPrice: stockOrder.stopPrice,
        // validity: stockOrder.validity,
        // validityDate: stockOrder.validityDate,
        status: getOrderStatusType(item['status'])
    };
    console.info('curOrder:', order)
    stockOrders.value.push(order)
  }
}

const handleStockCancel = () => {
  //一键取消股票订单
}

const queryAllStockOrders = async () => {
  const response = await axios.get('/v0/trade/order', params={'type': 0})
  console.info('queryAllStockOrders:', response.data)
  for (const item of response.data) {
    let status = 'pending'
    switch (item.status) {
    case 1: status = 'pending'; break;
    case 2: status = 'filled'; break;
    case 3: status = 'waiting'; break;
    default: break;
    }
    const order = {
      id: item.id,
      code: item.symbol,
      name: item.name,
      type: (item.direct === 0? 'buy': 'sell'),
      price: item.prices[0],
      quantity: item.quantity,
      orderType: 'limit',
      status: status,
      sysID: item.sysID
    }
    console.info('order:', order)
    stockOrders.value.push(order)
  }
}
onMounted(() => {
  // 首次查询当前订单
  queryAllStockOrders()
  sseService.on('update_position', onPositionUpdate)
  sseService.on('update_order', onOrderUpdate)
  sseService.on('order_success', onOrderSuccess)
})
onUnmounted(() => {
  // 清理处理器
  sseService.off('order_success', onOrderSuccess)
  sseService.off('update_position', onPositionUpdate)
  sseService.off('update_order', onOrderUpdate)
})
</script>

<style scoped>
.position-manager {
  background-color: var(--dark-bg);
  color: var(--text);
  min-height: 100vh;
  padding: 5px;
  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
}

.card {
  background-color: var(--panel-bg);
  border-radius: 8px;
  border: 1px solid var(--border);
  margin-bottom: 5px;
  overflow: hidden;
}

.card-header {
  border-bottom: 1px solid var(--border);
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.card-header h2 {
  margin: 0;
  font-size: 1.2rem;
  font-weight: 600;
}

.card-content {
  padding: 20px 0px 0px 0px;
  max-height: 70vh;
  overflow-y: auto;
}

.tab-navigation {
  display: flex;
  gap: 5px;
}

.tab-item {
  padding: 8px 16px;
  cursor: pointer;
  border-radius: 4px;
  font-size: 0.9rem;
  transition: all 0.2s;
}

.tab-item:hover {
  background-color: rgba(255, 255, 255, 0.05);
}

.tab-item.active {
  background-color: var(--primary);
  color: white;
}

.pane-header {
  margin-bottom: 5px;
  display: flex;
  justify-content: flex-end;
  gap: 5px;
}

.tab-pane {
  display: none;
}

.tab-pane.active {
  display: block;
}

.position-table {
  width: 100%;
  border-collapse: collapse;
  margin-bottom: 10px;
}

.position-table th,
.position-table td {
  padding: 12px 15px;
  text-align: left;
  border-bottom: 1px solid var(--border);
}

.position-table th {
  color: var(--text-secondary);
  font-weight: 500;
  font-size: 0.85rem;
}

.position-table tbody tr:hover {
  background-color: rgba(255, 255, 255, 0.03);
}

.long-position {
  background-color: rgba(0, 200, 83, 0.05);
}

.short-position {
  background-color: rgba(244, 67, 54, 0.05);
}

.direction-long {
  color: var(--secondary);
  font-weight: 500;
}

.direction-short {
  color: #f44336;
  font-weight: 500;
}

.btn {
  padding: 8px 16px;
  border: none;
  border-radius: 4px;
  cursor: pointer;
  font-size: 0.85rem;
  transition: all 0.2s;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}

.btn-small {
  padding: 6px 12px;
  font-size: 0.8rem;
}

.btn-mini {
  padding: 4px 8px;
  font-size: 0.75rem;
  margin-right: 5px;
}

.btn-primary {
  background-color: var(--primary);
  color: white;
}

.btn-secondary {
  background-color: #9c27b0;
  color: white;
}

.btn-danger {
  background-color: #f44336;
  color: white;
}

.btn-warning {
  background-color: #ff9800;
  color: white;
}

.btn-success {
  background-color: var(--secondary);
  color: white;
}

.profit-positive {
  color: var(--secondary);
}

.profit-negative {
  color: #f44336;
}

.operation-panel {
  margin-top: 20px;
  padding: 20px;
  border: 1px solid var(--border);
  border-radius: 6px;
  background-color: rgba(0, 0, 0, 0.2);
}

.operation-panel h3 {
  margin-top: 0;
  margin-bottom: 15px;
  font-size: 1.1rem;
}

.form-row {
  display: flex;
  gap: 15px;
  margin-bottom: 15px;
}

.form-group {
  flex: 1;
}

.form-group label {
  display: block;
  margin-bottom: 5px;
  color: var(--text-secondary);
  font-size: 0.85rem;
}

.form-control {
  width: 100%;
  padding: 8px 12px;
  background-color: var(--darker-bg);
  border: 1px solid var(--border);
  border-radius: 4px;
  color: var(--text);
  font-size: 0.9rem;
}

.form-control:focus {
  outline: none;
  border-color: var(--primary);
}

.operation-buttons {
  display: flex;
  gap: 10px;
}

..order-list {
  margin-top: 30px;
  border: 1px solid var(--border);
  border-radius: 6px;
  overflow: hidden;
}

.order-list .pane-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 15px 20px;
  background-color: rgba(0, 0, 0, 0.1);
  border-bottom: 1px solid var(--border);
  margin-bottom: 0;
}

.order-list .pane-header h3 {
  margin: 0;
  font-size: 1.1rem;
  font-weight: 600;
}

.order-table {
  width: 100%;
  border-collapse: collapse;
  margin-bottom: 0;
}

.order-table th,
.order-table td {
  padding: 12px 15px;
  text-align: left;
  border-bottom: 1px solid var(--border);
}

.order-table th {
  color: var(--text-secondary);
  font-weight: 500;
  font-size: 0.85rem;
  background-color: rgba(0, 0, 0, 0.05);
}

.order-table tbody tr:hover {
  background-color: rgba(255, 255, 255, 0.03);
}

.order-row {
  transition: all 0.2s;
}

.order-status {
  padding: 4px 8px;
  border-radius: 4px;
  font-size: 0.75rem;
  font-weight: 500;
}

.status-pending {
  background-color: #ff9800;
  color: white;
}

.status-filled {
  background-color: var(--secondary);
  color: white;
}

.status-cancelled {
  background-color: #757575;
  color: white;
}

.status-rejected {
  color: rgb(213, 9, 9);
}

.order-type-buy {
  color: var(--secondary);
}

.order-type-sell {
  color: #f44336;
}

.tab-container {
  margin-bottom: 20px;
}

.tab-header {
  display: flex;
  border-bottom: 1px solid var(--border);
  margin-bottom: 15px;
}

.tab-content {
  padding: 0;
}

.warning-banner {
  background-color: rgba(255, 152, 0, 0.1);
  border: 1px solid rgba(255, 152, 0, 0.3);
  color: #ff9800;
  padding: 10px 15px;
  border-radius: 4px;
  margin-bottom: 15px;
}

.checkbox-group {
  display: flex;
  align-items: center;
}

.checkbox-group input {
  margin-right: 8px;
}

.modal {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background-color: rgba(0, 0, 0, 0.7);
  display: flex;
  justify-content: center;
  align-items: center;
  z-index: 1000;
}

.modal-content {
  background-color: var(--panel-bg);
  padding: 20px;
  border-radius: 8px;
  width: 400px;
  max-width: 90%;
  border: 1px solid var(--border);
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 15px;
}

.modal-footer {
  display: flex;
  justify-content: flex-end;
  gap: 10px;
  margin-top: 20px;
}

.close {
  font-size: 20px;
  cursor: pointer;
  color: var(--text-secondary);
}

.system-monitor {
  margin-bottom: 10px;
}

.market-positions {
  margin-bottom: 20px;
}

/* 滚动条样式 */
.card-content::-webkit-scrollbar {
  width: 8px;
}

.card-content::-webkit-scrollbar-track {
  background: var(--darker-bg);
  border-radius: 4px;
}

.card-content::-webkit-scrollbar-thumb {
  background: var(--border);
  border-radius: 4px;
}

.card-content::-webkit-scrollbar-thumb:hover {
  background: var(--primary);
}

.order-list::-webkit-scrollbar {
  width: 6px;
}

.order-list::-webkit-scrollbar-track {
  background: var(--darker-bg);
  border-radius: 4px;
}

.order-list::-webkit-scrollbar-thumb {
  background: var(--border);
  border-radius: 4px;
}

.order-list::-webkit-scrollbar-thumb:hover {
  background: var(--primary);
}

.market-order-notice {
    padding: 8px 12px;
    background-color: rgba(41, 98, 255, 0.1);
    border: 1px solid rgba(41, 98, 255, 0.3);
    border-radius: 4px;
    color: var(--text);
    font-size: 0.85rem;
}

.order-type-badge {
    padding: 2px 6px;
    border-radius: 3px;
    font-size: 0.75rem;
    font-weight: 500;
}

.order-type-limit {
    background-color: rgba(41, 98, 255, 0.2);
    color: #2962ff;
}

.order-type-market {
    background-color: rgba(0, 200, 83, 0.2);
    color: #00c853;
}

.order-type-conditional {
    background-color: rgba(255, 109, 0, 0.2);
    color: #ff6d00;
}

.order-type-stop {
    background-color: rgba(244, 67, 54, 0.2);
    color: #f44336;
}
</style>