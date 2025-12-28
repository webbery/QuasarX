<template>
    <div class="trade-panel">
        <div class="panel-header">
            <h3>交易面板</h3>
        </div>
        
        <div class="form-container">
            <!-- 交易所 -->
            <div class="form-row">
                <label class="form-label">交易所</label>
                <select v-model="selectedExchange" class="form-select" @change="onExchangeChange">
                    <option value="SSE">上交所 (SSE)</option>
                    <option value="SZSE">深交所 (SZSE)</option>
                    <option value="BSE">北交所 (BSE)</option>
                    <option value="CFFEX">中金所 (CFFEX)</option>
                    <option value="SHFE">上期所 (SHFE)</option>
                    <option value="DCE">大商所 (DCE)</option>
                </select>
            </div>

            <!-- 种类 -->
            <div class="form-row">
                <label class="form-label">种类</label>
                <select v-model="selectedType" class="form-select" @change="onTypeChange">
                    <option value="stock">股票</option>
                    <option value="option">期权</option>
                    <option value="futures">期货</option>
                    <option value="fund">基金</option>
                </select>
            </div>

            <!-- 代码 -->
            <div class="form-row">
                <label class="form-label">代码</label>
                <input 
                    v-model="code" 
                    type="text" 
                    class="form-input" 
                    placeholder="输入证券代码" 
                    @input="onCodeChange"
                />
            </div>

            <!-- 名称 -->
            <div class="form-row">
                <label class="form-label">名称</label>
                <div class="form-display">{{ securityName || '--' }}</div>
            </div>

            <!-- 交易操作 (合并方向和开平) -->
            <div class="form-row">
                <label class="form-label">交易操作</label>
                <select v-model="selectedOperation" class="form-select" @change="onOperationChange">
                    <option value="buy_open">买入开仓</option>
                    <option value="sell_close">卖出平仓</option>
                    <option value="sell_open">卖出开仓</option>
                    <option value="buy_close">买入平仓</option>
                </select>
            </div>

            <!-- 类型 -->
            <div class="form-row">
                <label class="form-label">类型</label>
                <select v-model="selectedPriceType" class="form-select">
                    <option value="limit">限价</option>
                    <option value="market">市价</option>
                    <option value="condition">条件单</option>
                </select>
            </div>

            <!-- 价格和跟随最新复选框 -->
            <div class="form-row">
                <label class="form-label">价格</label>
                <div class="price-container">
                    <div class="price-input-wrapper">
                        <input 
                            v-model="price" 
                            type="number" 
                            class="form-input" 
                            :class="{ 'disabled-input': followLatestPrice }"
                            :disabled="followLatestPrice"
                            placeholder="0.00" 
                            step="0.01" 
                            @input="calculateAmount"
                        />
                        <span class="unit">{{ priceUnit }}</span>
                    </div>
                    <div class="follow-price-wrapper">
                        <input 
                            v-model="followLatestPrice" 
                            type="checkbox" 
                            id="follow-latest-price" 
                            @change="onFollowLatestPriceChange"
                        />
                        <label for="follow-latest-price" class="follow-label">跟随最新</label>
                    </div>
                </div>
            </div>

            <!-- 数量 -->
            <div class="form-row">
                <label class="form-label">数量</label>
                <div class="quantity-input-wrapper">
                    <input 
                        v-model="quantity" 
                        type="number" 
                        class="form-input" 
                        placeholder="0" 
                        min="0" 
                        @input="calculateAmount"
                    />
                    <span class="unit">{{ quantityUnit }}</span>
                </div>
            </div>

            <!-- 分隔线 -->
            <div class="divider"></div>

            <!-- 成本/收入 -->
            <div class="info-row">
                <span class="info-label">预估{{ selectedOperation.includes('buy') ? '成本' : '收入' }}</span>
                <span 
                    class="info-value amount"
                    :class="{ 'buy-color': selectedOperation.includes('buy'), 'sell-color': selectedOperation.includes('sell') }"
                >
                    {{ calculatedAmount }} {{ priceUnit }}
                </span>
            </div>

            <!-- 可用资金 -->
            <div class="info-row">
                <span class="info-label">可用资金</span>
                <span class="info-value balance">{{ availableFunds }} {{ priceUnit }}</span>
            </div>

            <!-- 结果信息 -->
            <div class="info-row">
                <span class="info-label">状态</span>
                <span class="info-value status">{{ statusMessage }}</span>
            </div>

            <!-- 提交按钮 -->
            <div class="action-row">
                <button class="submit-btn" @click="submitTrade">提交交易</button>
                <button class="reset-btn" @click="resetForm">重置</button>
            </div>
        </div>
    </div>
</template>

<script setup lang="ts">
import { ref, computed, watch, onMounted, onActivated } from 'vue'
import axios from 'axios';
import { getGlobalStorage } from '@/ts/globalStorage';

const globalStorage = getGlobalStorage()
// 响应式数据
const selectedExchange = ref('SSE')
const selectedType = ref('stock')
const code = ref('')
const securityName = ref('')
const selectedOperation = ref('buy_open')
const selectedPriceType = ref('limit')
const price = ref('')
const followLatestPrice = ref(false)
const quantity = ref('')
const availableFunds = ref('0')
const statusMessage = ref('等待输入')

// 计算属性
const quantityUnit = computed(() => {
    // 根据类型切换单位
    if (selectedType.value === 'option' || selectedType.value === 'futures') {
        return '手'
    }
    return '股'
})

const priceUnit = computed(() => {
    // 根据交易所和类型确定价格单位
    if (selectedExchange.value === 'CFFEX' || selectedExchange.value === 'SHFE' || selectedExchange.value === 'DCE') {
        return '元/手'
    }
    return '元'
})

const calculatedAmount = computed(() => {
    const priceValue = parseFloat(price.value) || 0
    const quantityValue = parseInt(quantity.value) || 0
    
    // 如果是期权/期货，1手通常代表不同数量的合约，这里简化处理
    let multiplier = 1
    if (selectedType.value === 'option' || selectedType.value === 'futures') {
        // 假设1手期权/期货对应100份合约
        multiplier = 100
    }
    
    return (priceValue * quantityValue * multiplier).toLocaleString()
})

onMounted(()=> {
    console.info('onMounted')
    updateCapital()
})

const parseLocalizedNumber = (localizedString: string) => {
    // 首先移除货币符号等无关字符
    let clean = localizedString.replace(/[^\d,\-.]/g, '');

    // 处理美式格式（如 "1,234,567.89"），直接移除千分位逗号
    clean = clean.replace(/,/g, '');
    return parseFloat(clean);
}

// 监听价格跟随最新选项变化
const onFollowLatestPriceChange = () => {
    if (followLatestPrice.value) {
        // 模拟获取最新价格
        const latestPrice = getLatestPrice()
        price.value = latestPrice.toString()
        calculateAmount()
    }
}

// 监听品种变化
const onTypeChange = () => {
    // 切换品种时，如果选择了期权/期货，默认数量为1手
    if (selectedType.value === 'option' || selectedType.value === 'futures') {
        if (!quantity.value || quantity.value === '0') {
            quantity.value = '1'
        }
    }
    
    // 更新状态消息
    statusMessage.value = `已切换为${selectedType.value === 'stock' ? '股票' : selectedType.value === 'option' ? '期权' : selectedType.value === 'futures' ? '期货' : '基金'}交易`
    calculateAmount()
}

// 监听操作变化
const onOperationChange = () => {
    // 根据操作更新状态消息
    const operationText = getOperationText(selectedOperation.value)
    statusMessage.value = `操作: ${operationText}`
    calculateAmount()
}

// 监听代码变化
const onCodeChange = async () => {
    if (code.value.length >= 6) {
        // 模拟根据代码获取证券名称和最新价格
        const securityInfo = await getSecurityInfo(code.value)
        securityName.value = securityInfo.name
        price.value = securityInfo.latestPrice.toString()
        
        statusMessage.value = `已加载 ${securityInfo.name}`
        calculateAmount()
    } else if (code.value.length === 0) {
        securityName.value = ''
        statusMessage.value = '等待输入'
    }
}

// 监听交易所变化
const onExchangeChange = () => {
    statusMessage.value = `交易所: ${selectedExchange.value}`
}

// 计算预估成本/收入
const calculateAmount = () => {
    // 计算逻辑已经在computed属性中处理
    // 这里可以添加其他计算逻辑
}

// 获取证券信息（模拟函数）
const getSecurityInfo = async (code: string) => {
    const securities: any = globalStorage.getItem('securities')
    if (selectedType.value === 'stock') {
        const stocks = securities.stocks
        const name = stocks.get(code)
        let info = undefined
        if (!!name) {
            const response = await axios.get('/v0/stocks/detail', {params: {
                id: code
            }})
            const data = response.data
            info = {
                name: name,
                latestPrice: data.price
            }
        }
        return info
    }
    return { name: '未知证券', latestPrice: 0 }
}

// 获取操作文本
const getOperationText = (operation: string): string => {
    const operationMap: Record<string, string> = {
        'buy_open': '买入开仓',
        'sell_close': '卖出平仓',
        'sell_open': '卖出开仓',
        'buy_close': '买入平仓'
    }
    return operationMap[operation] || operation
}

// 提交交易
const submitTrade = async () => {
    if (!price.value || parseFloat(price.value) <= 0) {
        statusMessage.value = '错误: 价格必须大于0'
        return
    }
    
    if (!quantity.value || parseInt(quantity.value) <= 0) {
        statusMessage.value = '错误: 数量必须大于0'
        return
    }
    
    if ('buy_open' === selectedOperation.value || selectedOperation.value === 'buy_close') {
        const capital = parseLocalizedNumber(availableFunds.value)
        const ammount = parseLocalizedNumber(calculatedAmount.value)
        if (ammount >= capital) {
            statusMessage.value = '错误: 成本必须小于可用资金'
            return
        }
    }
    let order
    switch (selectedType.value) {
    case 'stock':
        order = generateSockOrder()
    break;
    case 'option':
    break;
    default:
        statusMessage.value = `暂不支持的证券类型`
        return;
    }
    const operationText = getOperationText(selectedOperation.value)
    statusMessage.value = `正在提交${operationText}订单...`
    
    // 提交交易
    try{
        const res = await axios.post('/v0/trade/order', order)
        console.info('trade order:', res)
        // TODO: 发送消息更新持仓信息
        statusMessage.value = `${operationText}订单提交成功`
        // TODO:更新可用资金
        await updateCapital()
    } catch (error) {
        console.info('error:')
        statusMessage.value = `${operationText}订单提交失败`
    }
}

const generateSockOrder = () => {
    let direct = 0
    let open = 0
    if (selectedOperation.value === 'buy_open' || selectedOperation.value === 'buy_close') {
        direct = 0
    } else {
        direct = 1
    }
    if (selectedOperation.value === 'buy_close' || selectedOperation.value === 'sell_close') {
        open = 1
    }
    let type = 0
    switch (selectedPriceType.value) {
    case 'limit':
        type = 1
        break;
    case 'market':
        type = 0
        break;
    case 'condition':
        type = 2
        break
    default:
        break;
    }
    
    const order = {
        id: Date.now(),
        symbol: code.value,
        name: securityName.value,
        direct: direct,
        kind: 0,
        type: type,
        prices: [parseFloat(price.value)],
        quantity: parseInt(quantity.value),
        open: open
        // conditionType: conditionType,
        // triggerPrice: parseFloat(newStockOrder.triggerPrice),
        // stopPrice: parseFloat(newStockOrder.stopPrice),
        // validity: newStockOrder.validity,
        // validityDate: newStockOrder.validityDate
    };
    return order    
}
// 重置表单
const resetForm = () => {
    price.value = ''
    quantity.value = ''
    followLatestPrice.value = false
    statusMessage.value = '等待输入'
}

const updateCapital = async () => {
    const response = await axios.get('/v0/user/funds')
    const data = response.data
    availableFunds.value = data.funds.toLocaleString() || 0
}

</script>

<style scoped>
.trade-panel {
    background-color: var(--panel-bg);
    border-radius: 8px;
    border: 1px solid var(--border);
    overflow: hidden;
    height: 100%;
    display: flex;
    flex-direction: column;
}

.panel-header {
    background: linear-gradient(90deg, var(--darker-bg) 0%, var(--panel-bg) 100%);
    border-bottom: 1px solid var(--border);
    padding: 16px 20px;
}

.panel-header h3 {
    color: var(--text);
    font-size: 16px;
    font-weight: 600;
    margin: 0;
}

.form-container {
    padding: 20px;
    flex: 1;
    display: flex;
    flex-direction: column;
    gap: 16px;
}

.form-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    min-height: 40px;
}

.form-label {
    color: var(--text-secondary);
    font-size: 14px;
    min-width: 80px;
    text-align: right;
    padding-right: 16px;
}

.form-select {
    flex: 1;
    background-color: var(--darker-bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    color: var(--text);
    padding: 8px 12px;
    font-size: 14px;
    outline: none;
    transition: border-color 0.2s;
    height: 36px;
}

.form-select:focus {
    border-color: var(--primary);
}

.form-input {
    flex: 1;
    background-color: var(--darker-bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    color: var(--text);
    padding: 8px 12px;
    font-size: 14px;
    outline: none;
    transition: border-color 0.2s;
    height: 36px;
}

.form-input:focus {
    border-color: var(--primary);
}

.form-input::placeholder {
    color: var(--text-secondary);
    opacity: 0.6;
}

.form-input:disabled {
    opacity: 0.6;
    cursor: not-allowed;
}

.disabled-input {
    background-color: rgba(42, 52, 73, 0.5);
    color: var(--text-secondary);
}

.form-display {
    flex: 1;
    background-color: var(--darker-bg);
    border: 1px solid var(--border);
    border-radius: 4px;
    color: var(--text);
    padding: 8px 12px;
    font-size: 14px;
    height: 36px;
    display: flex;
    align-items: center;
}

/* 价格容器，包含输入框和跟随复选框 */
.price-container {
    flex: 1;
    display: flex;
    align-items: center;
    gap: 12px;
}

.price-input-wrapper {
    flex: 1;
    display: flex;
    position: relative;
}

.quantity-input-wrapper {
    flex: 1;
    display: flex;
    position: relative;
}

.price-input-wrapper .unit,
.quantity-input-wrapper .unit {
    position: absolute;
    right: 12px;
    top: 50%;
    transform: translateY(-50%);
    color: var(--text-secondary);
    font-size: 12px;
    pointer-events: none;
}

/* 跟随最新价格复选框 */
.follow-price-wrapper {
    display: flex;
    align-items: center;
    gap: 6px;
    white-space: nowrap;
    margin-left: auto;
}

.follow-price-wrapper input[type="checkbox"] {
    width: 16px;
    height: 16px;
    accent-color: var(--primary);
    cursor: pointer;
}

.follow-label {
    color: var(--text-secondary);
    font-size: 14px;
    cursor: pointer;
    user-select: none;
}

.divider {
    height: 1px;
    background-color: var(--border);
    margin: 8px 0;
    opacity: 0.5;
}

.info-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 8px 0;
}

.info-label {
    color: var(--text-secondary);
    font-size: 14px;
}

.info-value {
    font-size: 14px;
    font-weight: 500;
}

.info-value.amount {
    color: var(--text);
}

.buy-color {
    color: var(--secondary) !important;
}

.sell-color {
    color: #ff3d3d !important;
}

.info-value.balance {
    color: var(--secondary);
}

.info-value.status {
    color: var(--accent);
}

.action-row {
    display: flex;
    gap: 12px;
    margin-top: auto;
    padding-top: 20px;
}

.submit-btn {
    flex: 1;
    background: linear-gradient(90deg, var(--primary), #3d7eff);
    color: white;
    border: none;
    border-radius: 4px;
    padding: 12px 0;
    font-size: 14px;
    font-weight: 600;
    cursor: pointer;
    transition: opacity 0.2s;
}

.submit-btn:hover {
    opacity: 0.9;
}

.reset-btn {
    flex: 1;
    background-color: transparent;
    color: var(--text-secondary);
    border: 1px solid var(--border);
    border-radius: 4px;
    padding: 12px 0;
    font-size: 14px;
    cursor: pointer;
    transition: all 0.2s;
}

.reset-btn:hover {
    background-color: var(--darker-bg);
    color: var(--text);
}
</style>