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
              <div class="pane-header">
                <button class="btn btn-primary btn-small" @click="refreshStockPositions">刷新</button>
                <button class="btn btn-secondary btn-small" @click="openNewStockOperation">新代码买入</button>
              </div>
              
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
                  <tr v-for="position in stockPositions" :key="position.code">
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
                      <button class="btn btn-mini" @click="openStockOperation(position, 'buy')">买入</button>
                      <button class="btn btn-mini" @click="openStockOperation(position, 'buy_margin')">融资买入</button>
                      <button class="btn btn-mini btn-danger" @click="openStockOperation(position, 'sell')">卖出</button>
                      <button class="btn btn-mini btn-danger" @click="openStockOperation(position, 'sell_short')">融券卖出</button>
                    </td>
                  </tr>
                </tbody>
              </table>
              
              <!-- 新代码买入面板 -->
              <div class="operation-panel" v-if="newStockOperation">
                <h3>新代码买入</h3>
                
                <div class="form-row">
                  <div class="form-group">
                    <label>股票代码</label>
                    <input type="text" class="form-control" v-model="newStockOrder.code" placeholder="请输入股票代码">
                  </div>
                  <div class="form-group">
                    <label>股票名称</label>
                    <input type="text" class="form-control" v-model="newStockOrder.name" readonly="readonly"></input>
                  </div>
                  <div class="form-group">
                    <label>交易类型</label>
                    <select class="form-control" v-model="newStockOrder.tradeType">
                      <option value="buy">普通买入</option>
                      <option value="buy_margin">融资买入</option>
                    </select>
                  </div>
                  <div class="form-group">
                    <label>订单类型</label>
                    <select class="form-control" v-model="newStockOrder.orderType" @change="handleOrderTypeChange('new')">
                      <option value="limit">限价单</option>
                      <option value="market">市价单</option>
                      <option value="conditional">条件单</option>
                      <option value="stop">止损单</option>
                    </select>
                  </div>
                </div>
                
                <!-- 价格输入区域 - 根据订单类型动态显示 -->
                <div class="form-row" v-if="newStockOrder.orderType === 'limit'">
                  <div class="form-group">
                    <label>限价价格</label>
                    <input type="text" class="form-control" v-model="newStockOrder.price" placeholder="请输入限价价格">
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="newStockOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                
                <div class="form-row" v-if="newStockOrder.orderType === 'market'">
                  <div class="form-group">
                    <label>市价单说明</label>
                    <div class="market-order-notice">市价单将以最优市场价格立即成交</div>
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="newStockOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                
                <div class="form-row" v-if="newStockOrder.orderType === 'conditional'">
                  <div class="form-group">
                    <label>触发条件</label>
                    <select class="form-control" v-model="newStockOrder.conditionType">
                      <option value="price_ge">价格≥</option>
                      <option value="price_le">价格≤</option>
                      <option value="time">时间条件</option>
                    </select>
                  </div>
                  <div class="form-group">
                    <label>触发价格</label>
                    <input type="text" class="form-control" v-model="newStockOrder.triggerPrice" placeholder="触发价格">
                  </div>
                  <div class="form-group">
                    <label>订单价格</label>
                    <input type="text" class="form-control" v-model="newStockOrder.price" placeholder="触发后执行价格">
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="newStockOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                
                <div class="form-row" v-if="newStockOrder.orderType === 'stop'">
                  <div class="form-group">
                    <label>止损价格</label>
                    <input type="text" class="form-control" v-model="newStockOrder.stopPrice" placeholder="止损触发价格">
                  </div>
                  <div class="form-group">
                    <label>订单价格</label>
                    <input type="text" class="form-control" v-model="newStockOrder.price" placeholder="止损后执行价格">
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="newStockOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                <!-- 有效期设置 -->
                <div class="form-row" v-if="newStockOrder.orderType !== 'market'">
                  <div class="form-group">
                    <label>有效期</label>
                    <select class="form-control" v-model="newStockOrder.validity">
                      <option value="day">当日有效</option>
                      <option value="week">本周有效</option>
                      <option value="month">本月有效</option>
                      <option value="gtd">指定日期前有效</option>
                    </select>
                  </div>
                  <div class="form-group" v-if="newStockOrder.validity === 'gtd'">
                    <label>有效日期</label>
                    <input type="date" class="form-control" v-model="newStockOrder.validityDate">
                  </div>
                </div>

                <div class="warning-banner" v-if="newStockOrder.tradeType === 'buy_margin' && !capitalChecked">
                  请先进行验资操作
                </div>
                
                <div class="operation-buttons">
                  <button class="btn btn-primary" @click="checkCapital" v-if="newStockOrder.tradeType === 'buy_margin'">验资</button>
                  <button class="btn btn-success" :disabled="!canPlaceNewStockOrder" @click="placeNewStockOrder">提交订单</button>
                  <button class="btn" @click="closeNewStockOperation">取消</button>
                </div>
              </div>
              
              <!-- 股票操作面板 -->
              <div class="operation-panel" v-if="selectedStock && !newStockOperation">
                <h3>{{ selectedStock.name }} ({{ selectedStock.code }}) - {{ stockOperationTypeText }}</h3>
                
                <div class="form-row">
                  <div class="form-group">
                    <label>订单类型</label>
                    <select class="form-control" v-model="stockOrder.orderType" @change="handleOrderTypeChange('existing')">
                      <option value="limit">限价单</option>
                      <option value="market">市价单</option>
                      <option value="conditional">条件单</option>
                      <option value="stop">止损单</option>
                    </select>
                  </div>
                </div>
                
                <!-- 价格输入区域 - 根据订单类型动态显示 -->
                <div class="form-row" v-if="stockOrder.orderType === 'limit'">
                  <div class="form-group">
                    <label>限价价格</label>
                    <input type="text" class="form-control" v-model="stockOrder.price" placeholder="请输入限价价格">
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="stockOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                
                <div class="form-row" v-if="stockOrder.orderType === 'market'">
                  <div class="form-group">
                    <label>市价单说明</label>
                    <div class="market-order-notice">市价单将以最优市场价格立即成交</div>
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="stockOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                
                <div class="form-row" v-if="stockOrder.orderType === 'conditional'">
                  <div class="form-group">
                    <label>触发条件</label>
                    <select class="form-control" v-model="stockOrder.conditionType">
                      <option value="price_ge">价格≥</option>
                      <option value="price_le">价格≤</option>
                      <option value="time">时间条件</option>
                    </select>
                  </div>
                  <div class="form-group">
                    <label>触发价格</label>
                    <input type="text" class="form-control" v-model="stockOrder.triggerPrice" placeholder="触发价格">
                  </div>
                </div>
                
                <div class="form-row" v-if="stockOrder.orderType === 'conditional'">
                  <div class="form-group">
                    <label>订单价格</label>
                    <input type="text" class="form-control" v-model="stockOrder.price" placeholder="触发后执行价格">
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="stockOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                
                <div class="form-row" v-if="stockOrder.orderType === 'stop'">
                  <div class="form-group">
                    <label>止损价格</label>
                    <input type="text" class="form-control" v-model="stockOrder.stopPrice" placeholder="止损触发价格">
                  </div>
                  <div class="form-group">
                    <label>订单价格</label>
                    <input type="text" class="form-control" v-model="stockOrder.price" placeholder="止损后执行价格">
                  </div>
                </div>
                
                <div class="form-row" v-if="stockOrder.orderType === 'stop'">
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="stockOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                
                <!-- 有效期设置 -->
                <div class="form-row" v-if="stockOrder.orderType !== 'market'">
                  <div class="form-group">
                    <label>有效期</label>
                    <select class="form-control" v-model="stockOrder.validity">
                      <option value="day">当日有效</option>
                      <option value="week">本周有效</option>
                      <option value="month">本月有效</option>
                      <option value="gtd">指定日期前有效</option>
                    </select>
                  </div>
                  <div class="form-group" v-if="stockOrder.validity === 'gtd'">
                    <label>有效日期</label>
                    <input type="date" class="form-control" v-model="stockOrder.validityDate">
                  </div>
                </div>
                
                <div class="warning-banner" v-if="(stockOperationType === 'buy' || stockOperationType === 'buy_margin') && !capitalChecked">
                  请先进行验资操作
                </div>
                
                <div class="warning-banner" v-if="(stockOperationType === 'sell' || stockOperationType === 'sell_short') && !stockChecked">
                  请先进行验券操作
                </div>
                
                <div class="operation-buttons">
                  <button class="btn btn-primary" @click="checkCapital" v-if="stockOperationType === 'buy' || stockOperationType === 'buy_margin'">验资</button>
                  <button class="btn btn-primary" @click="checkStock" v-if="stockOperationType === 'sell' || stockOperationType === 'sell_short'">验券</button>
                  <button class="btn btn-success" :disabled="!canPlaceStockOrder" @click="placeStockOrder">提交订单</button>
                  <button class="btn" @click="closeStockOperation">取消</button>
                </div>
              </div>
              
              <!-- 股票委托列表 -->
              <div class="order-list">
                <h3>股票委托</h3>
                <div class="order-item" v-for="order in stockOrders" :key="order.id">
                  <div>
                    <span>{{ order.code }} {{ order.name }}</span> | 
                    <span :class="getOrderTypeClass(order.type)">{{ getOrderTypeText(order.type) }}</span> | 
                    <span class="order-type-badge" :class="`order-type-${order.orderType}`">{{ getOrderTypeDisplayText(order.orderType) }}</span> |
                    <span v-if="order.orderType === 'limit'">{{ order.price }} × {{ order.quantity }}</span>
                    <span v-if="order.orderType === 'market'">市价 × {{ order.quantity }}</span>
                    <span v-if="order.orderType === 'conditional'">触发:{{ order.triggerPrice }} 执行:{{ order.price }} × {{ order.quantity }}</span>
                    <span v-if="order.orderType === 'stop'">止损:{{ order.stopPrice }} 执行:{{ order.price }} × {{ order.quantity }}</span>
                  </div>
                  <div>
                    <span class="order-status" :class="`status-${order.status}`">
                      {{ getOrderStatusText(order) }}
                    </span>
                    <button class="btn btn-mini" @click="cancelOrder(order)" v-if="order.status === 'pending' || order.status === 'waiting'">撤单</button>
                  </div>
                </div>
              </div>
            </div>
            
            <!-- 期权持仓 -->
            <div class="tab-pane" :class="{active: activeMarketTab === 'option'}">
              <div class="pane-header">
                <button class="btn btn-primary btn-small" @click="refreshOptionPositions">刷新</button>
                <button class="btn btn-secondary btn-small" @click="openNewOptionOperation">新合约买入</button>
              </div>
              
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
              
              <!-- 新合约买入面板 -->
              <div class="operation-panel" v-if="newOptionOperation">
                <h3>新合约买入</h3>
                
                <div class="form-row">
                  <div class="form-group">
                    <label>合约代码</label>
                    <input type="text" class="form-control" v-model="newOptionOrder.code" placeholder="请输入合约代码">
                  </div>
                  <div class="form-group">
                    <label>合约名称</label>
                    <input type="text" class="form-control" v-model="newOptionOrder.name" placeholder="请输入合约名称">
                  </div>
                </div>
                
                <div class="form-row">
                  <div class="form-group">
                    <label>操作类型</label>
                    <select class="form-control" v-model="newOptionOrder.type">
                      <option value="buyOpen">买开</option>
                      <option value="sellOpen">卖开</option>
                    </select>
                  </div>
                  <div class="form-group">
                    <label>价格</label>
                    <input type="text" class="form-control" v-model="newOptionOrder.price" placeholder="请输入价格">
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="newOptionOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                
                <div class="operation-buttons">
                  <button class="btn btn-success" @click="placeNewOptionOrder">提交订单</button>
                  <button class="btn" @click="closeNewOptionOperation">取消</button>
                </div>
              </div>
              
              <!-- 期权操作面板 -->
              <div class="operation-panel" v-if="selectedOption && !newOptionOperation">
                <h3>{{ selectedOption.name }} ({{ selectedOption.code }}) - {{ optionOperationTypeText }}</h3>
                
                <div class="form-row">
                  <div class="form-group">
                    <label>价格</label>
                    <input type="text" class="form-control" v-model="optionOrder.price" placeholder="请输入价格">
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="optionOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                
                <div class="operation-buttons">
                  <button class="btn btn-success" @click="placeOptionOrder">提交订单</button>
                  <button class="btn" @click="closeOptionOperation">取消</button>
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
              <div class="pane-header">
                <button class="btn btn-primary btn-small" @click="refreshFuturePositions">刷新</button>
                <button class="btn btn-secondary btn-small" @click="openNewFutureOperation">新合约买入</button>
              </div>
              
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
              
              <!-- 新合约买入面板 -->
              <div class="operation-panel" v-if="newFutureOperation">
                <h3>新合约买入</h3>
                
                <div class="form-row">
                  <div class="form-group">
                    <label>合约代码</label>
                    <input type="text" class="form-control" v-model="newFutureOrder.code" placeholder="请输入合约代码">
                  </div>
                  <div class="form-group">
                    <label>合约名称</label>
                    <input type="text" class="form-control" v-model="newFutureOrder.name" placeholder="请输入合约名称">
                  </div>
                </div>
                
                <div class="form-row">
                  <div class="form-group">
                    <label>方向</label>
                    <select class="form-control" v-model="newFutureOrder.direction">
                      <option value="long">多头</option>
                      <option value="short">空头</option>
                    </select>
                  </div>
                  <div class="form-group">
                    <label>价格</label>
                    <input type="text" class="form-control" v-model="newFutureOrder.price" placeholder="请输入价格">
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="newFutureOrder.quantity" placeholder="请输入数量">
                  </div>
                </div>
                
                <div class="operation-buttons">
                  <button class="btn btn-success" @click="placeNewFutureOrder">提交订单</button>
                  <button class="btn" @click="closeNewFutureOperation">取消</button>
                </div>
              </div>
              
              <!-- 期货操作面板 -->
              <div class="operation-panel" v-if="selectedFuture && !newFutureOperation">
                <h3>{{ selectedFuture.name }} ({{ selectedFuture.code }}) - {{ futureOperationType === 'open' ? '开仓' : '平仓' }}</h3>
                
                <div class="form-row">
                  <div class="form-group">
                    <label>价格</label>
                    <input type="text" class="form-control" v-model="futureOrder.price" placeholder="请输入价格">
                  </div>
                  <div class="form-group">
                    <label>数量</label>
                    <input type="text" class="form-control" v-model="futureOrder.quantity" placeholder="请输入数量">
                  </div>
                  <div class="form-group" v-if="futureOperationType === 'open'">
                    <label>方向</label>
                    <select class="form-control" v-model="futureOrder.direction">
                      <option value="long">多头</option>
                      <option value="short">空头</option>
                    </select>
                  </div>
                </div>
                
                <div class="operation-buttons">
                  <button class="btn btn-success" @click="placeFutureOrder">提交订单</button>
                  <button class="btn" @click="closeFutureOperation">取消</button>
                </div>
              </div>
              
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
import { ref, computed, onMounted, reactive, onUnmounted } from 'vue'
import axios from 'axios';
import sseService from '@/ts/SSEService';
import { message } from '@/tool';

// 市场选项卡
const activeMarketTab = ref('stock');

// 股票相关数据
const stockPositions = ref([
    { code: '000001', name: '平安银行', quantity: 1000, available: 800, costPrice: 15.20, currentPrice: 16.50, profit: 1300 },
    { code: '600036', name: '招商银行', quantity: 500, available: 500, costPrice: 38.50, currentPrice: 40.20, profit: 850 }
]);
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
    quantity: '',
    conditionType: 'price_ge', // 条件单类型
    triggerPrice: '', // 触发价格
    stopPrice: '', // 止损价格
    validity: 'day', // 有效期
    validityDate: '' // 指定日期
});

const stockOrders = ref([
    { 
        id: 1, 
        code: '000001', 
        name: '平安银行', 
        type: 'buy', 
        orderType: 'limit',
        price: 16.30, 
        quantity: 200, 
        status: 'pending' 
    },
    { 
        id: 2, 
        code: '600036', 
        name: '招商银行', 
        type: 'sell', 
        orderType: 'market',
        price: 0, 
        quantity: 100, 
        status: 'filled' 
    },
    { 
        id: 3, 
        code: '000858', 
        name: '五粮液', 
        type: 'buy_margin', 
        orderType: 'conditional',
        price: 145.00, 
        triggerPrice: 150.00,
        quantity: 100, 
        status: 'waiting' 
    },
    { 
        id: 4, 
        code: '600519', 
        name: '贵州茅台', 
        type: 'sell', 
        orderType: 'stop',
        price: 1800.00, 
        stopPrice: 1750.00,
        quantity: 50, 
        status: 'waiting' 
    }
]);

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

const getOrderStatusText = (order) => {
    switch(order.status) {
        case 'pending': return '待成交';
        case 'filled': return '已成交';
        case 'cancelled': return '已撤单';
        case 'waiting': 
            return order.orderType === 'conditional' ? '等待触发' : 
                   order.orderType === 'stop' ? '监控中' : '待成交';
        default: return order.status;
    }
};

const getOrderTypeDisplayText = (orderType) => {
    switch(orderType) {
        case 'limit': return '限价';
        case 'market': return '市价';
        case 'conditional': return '条件';
        case 'stop': return '止损';
        default: return orderType;
    }
};

// 方法
const refreshStockPositions = () => {
    showMessage('股票持仓已刷新', 'success');
};

const refreshOptionPositions = () => {
    showMessage('期权持仓已刷新', 'success');
};

const refreshFuturePositions = () => {
    showMessage('期货持仓已刷新', 'success');
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
        showMessage('验资通过', 'success');
    });
};

const checkStock = () => {
    openModal('验券确认', '确定要进行验券操作吗？', () => {
        stockChecked.value = true;
        showMessage('验券通过', 'success');
    });
};

const placeStockOrder = () => {
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
        status: stockOrder.orderType === 'limit' || stockOrder.orderType === 'market' ? 'pending' : 'waiting'
    };
    console.info('stock commit', order)
    
    stockOrders.value.unshift(order);
    showMessage('股票订单提交成功', 'success');
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
      case 'limit': type = 0; break;
      case 'market': type = 1; break;
      default: type = 0; break;
    }
    const order = {
        id: Date.now(),
        symbol: newStockOrder.code,
        name: newStockOrder.name,
        direct: direct,
        kind: 0,
        type: type,
        prices: [newStockOrder.price],
        quantity: newStockOrder.quantity,
        conditionType: newStockOrder.conditionType,
        triggerPrice: newStockOrder.triggerPrice,
        stopPrice: newStockOrder.stopPrice,
        validity: newStockOrder.validity,
        validityDate: newStockOrder.validityDate
    };
    console.info('buy:', order)
    try{
      await axios.post('/v0/trade/order', order)
      stockOrders.value.unshift(order);
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
    showMessage('期权订单提交成功', 'success');
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
    showMessage('新合约买入订单提交成功', 'success');
    closeNewOptionOperation();
};

const saveOptionSettings = () => {
    showMessage('期权参数设置已保存', 'success');
};

const openNewFutureOperation = () => {
    newFutureOperation.value = true;
    selectedFuture.value = null;
    futureOperationType.value = '';
    newFutureOrder.code = '';
    newFutureOrder.name = '';
    newFutureOrder.direction = 'long';
    newFutureOrder.price = '';
    newFutureOrder.quantity = '';
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
    showMessage('期货订单提交成功', 'success');
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
    showMessage('新合约买入订单提交成功', 'success');
    closeNewFutureOperation();
};

const cancelOrder = (order) => {
    openModal('撤单确认', '确定要撤销此订单吗？', () => {
        order.status = 'cancelled';
        showMessage('订单已撤销', 'success');
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
        
        showMessage('所有待成交订单已撤销', 'success');
    });
};

const handleEmergency = () => {
    openModal('应急处置确认', '确定要执行应急处置操作吗？此操作将暂停所有交易功能。', () => {
        tradePermission.value = 'disabled';
        showMessage('应急处置已执行，交易功能已暂停', 'success');
    });
};

const handleErrorReport = () => {
    showMessage('错误报告已生成，请联系技术支持', 'info');
};

const handleEmergencyStop = () => {
    openModal('紧急停止确认', '确定要紧急停止所有交易吗？此操作不可逆。', () => {
        tradePermission.value = 'disabled';
        abnormalTradeDetected.value = true;
        showMessage('交易已紧急停止', 'success');
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

// 消息提示
const showMessage = (message, type) => {
    // 在实际项目中，这里可以集成更复杂的消息提示组件
    alert(`${type === 'success' ? '成功' : '信息'}: ${message}`);
};

const onOrderSuccess = (message) => {
  console.info('onOrderSuccess sse: ', message)
  if (message.type === 'order_success') {
  }
}
onMounted(() => {
  sseService.on('order_success', onOrderSuccess)
})
onUnmounted(() => {
  // 清理处理器
  sseService.off('order_success', onOrderSuccess)
})
</script>

<style scoped>
.position-manager {
  background-color: var(--dark-bg);
  color: var(--text);
  min-height: 100vh;
  padding: 20px;
  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
}

.card {
  background-color: var(--panel-bg);
  border-radius: 8px;
  border: 1px solid var(--border);
  margin-bottom: 20px;
  overflow: hidden;
}

.card-header {
  padding: 15px 20px;
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
  padding: 20px;
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
  margin-bottom: 15px;
  display: flex;
  justify-content: flex-end;
  gap: 10px;
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
  margin-bottom: 20px;
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

.order-list {
  margin-top: 30px;
}

.order-list h3 {
  margin-bottom: 15px;
  font-size: 1.1rem;
}

.order-item {
  padding: 12px 15px;
  border: 1px solid var(--border);
  border-radius: 4px;
  margin-bottom: 10px;
  display: flex;
  justify-content: space-between;
  align-items: center;
  background-color: rgba(0, 0, 0, 0.1);
}

.order-status {
  padding: 3px 8px;
  border-radius: 4px;
  font-size: 0.75rem;
  margin-right: 10px;
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
  margin-bottom: 20px;
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