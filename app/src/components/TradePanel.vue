<template>
    <div class="trade-panel" :class="{ 'disabled': isSubmitting }">
        <div v-if="isSubmitting" class="submit-overlay">
            <div class="loading-spinner"></div>
            <div class="loading-text">正在提交...</div>
        </div>

        <div class="panel-header" :class="{'highlighted': isHighlighted}" >
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
                <label class="form-label" >名称</label>
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
import { ref, computed, watch, onMounted, onActivated, defineProps } from 'vue'
import axios from 'axios';
import { getGlobalStorage } from '@/ts/globalStorage';
import getZh from '@/ts/i18n';
import {errorCode,  ErrorCodeMap} from '@/ts/ErrorCode';

interface SecurityInfo {
  name: string;
  latestPrice: number;
  upper: number;
  lower: number;
}

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
const statusMessage = ref('')
const isSubmitting = ref(false)
// 高亮状态
const isHighlighted = ref(false);

let upper = 0
let lower = 0

const props = defineProps({
    selectedSecurity: {
        type: Object,
        default: null
    },
    highlight: {
        type: Boolean,
        default: false
    }
});

// 监听选中的股票变化
watch(() => props.selectedSecurity, async (newStock) => {
    if (newStock) {
        console.log('收到选中的股票:', newStock);
        
        // 自动填充信息
        const exchangeMap: Record<string, string> = {
            '深交所': 'SZSE',
            '上交所': 'SSE',
            '北交所': 'BSE'
        };
        
        selectedExchange.value = exchangeMap[newStock.exchange] || 'SSE';
        selectedType.value = 'stock';
        code.value = newStock.code.split('.')[1] || newStock.code;
        securityName.value = newStock.name;
        
        // 根据操作类型设置交易操作
        if (newStock.operationType === 'buy' || newStock.operationType === 'buy_margin') {
            selectedOperation.value = 'buy_open';
        } else if (newStock.operationType === 'sell' || newStock.operationType === 'sell_short') {
            selectedOperation.value = 'sell_close';
        }
        
        // 设置价格
        price.value = newStock.currentPrice;
        followLatestPrice.value = true;
        statusMessage.value = `已选择 ${newStock.name} (${newStock.code})`;
        // 更新上下限
        const securityInfo = await getSecurityInfo(code.value)
        upper = securityInfo.upper
        lower = securityInfo.lower
    }
}, { immediate: true });

// 监听高亮状态
watch(() => props.highlight, (newVal) => {
    isHighlighted.value = newVal;
});

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
        const latestPrice = price.value || 0
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
        // 根据代码获取证券名称和最新价格
        try {
            const securityInfo = await getSecurityInfo(code.value)
            if (securityInfo === undefined) {
                statusMessage.value = '错误: 无法获取证券信息'
                securityName.value = ''
                price.value = ''
                upper = 0
                lower = 0
            } else {
                securityName.value = securityInfo.name
                price.value = securityInfo.latestPrice.toString()
                upper = securityInfo.upper
                lower = securityInfo.lower

                statusMessage.value = `已加载 ${securityInfo.name}`
                calculateAmount()
            }
        } catch (err: any) {
            const response = err.response
            const data = response.data
        }
    } else if (code.value.length === 0) {
        securityName.value = ''
        statusMessage.value = ''
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
const getSecurityInfo = async (code: string): Promise<SecurityInfo|undefined> => {
    const securities: any = globalStorage.getItem('securities')
    if (selectedType.value === 'stock') {
        const stocks = securities.stocks
        const name = stocks.get(code)
        if (!!name) {
            const response = await axios.get('/v0/stocks/detail', {params: {
                id: code
            }})
            const data = response.data
            return {
                name: name,
                latestPrice: data.price,
                upper: data.upper,
                lower: data.lower
            }
        }
    }
    return undefined
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

const checkStockPrivilege = async (code: string) => {
    try {
        const response = await axios.get('/v0/stocks/privilege', {params: {
            id: code
        }})
        if (response.status === 200) {
            const data = response.data
            if (data['forbid']) {
                statusMessage.value = '错误: ' + getZh(data['message'])
                return false
            }
        }
    } catch (err: any) {
        // console.info('err: ', err)
        const response = err.response
        if (response.status === 400) {
            const data = response.data
            const code = data['status']
            if (typeof code === 'string' && code in errorCode) {
                statusMessage.value = '错误: ' + errorCode[code as keyof ErrorCodeMap]
            } else {
                statusMessage.value = '未知错误:' + code
            }
            return false
        }
    }
    
    return true
}
// 提交交易
const submitTrade = async () => {
    if (isSubmitting.value) return;

    let priceValue = parseFloat(price.value)
    if (!price.value || priceValue <= 0) {
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
    if (priceValue > upper) {
        statusMessage.value = '错误: 买入价超过涨停价'
        return
    }
    if (priceValue < lower) {
        statusMessage.value = '错误: 买入价低于跌停价'
        return
    }
    isSubmitting.value = true
    const operationText = getOperationText(selectedOperation.value)
    statusMessage.value = `正在提交${operationText}订单...`

    let order
    switch (selectedType.value) {
    case 'stock':
        // 检查交易权限
        if (!await checkStockPrivilege(code.value)) {
            isSubmitting.value = false
            return
        }
        order = generateSockOrder()
    break;
    case 'option':
    break;
    default:
        statusMessage.value = `暂不支持的证券类型`
        isSubmitting.value = false
        return;
    }
    
    // 提交交易
    try{
        const res = await axios.post('/v0/trade/order', order)
        console.info('trade order:', res)
        // TODO: 发送消息更新持仓信息
        statusMessage.value = `${operationText}订单提交成功`
        // TODO:更新可用资金
        await updateCapital()
    } catch (error) {
        statusMessage.value = `${operationText}订单提交失败`
        if (axios.isAxiosError(error)) {
            if (error.response) {
                const data = error.response.data
                const code = data['status']
                if (code in errorCode) {
                    statusMessage.value = `${operationText}订单提交失败: ${errorCode[code as keyof ErrorCodeMap]}`
                } else {
                    statusMessage.value = '服务器错误: ' + (data['message'] || '未知错误')
                }
            }
            else if (error.request) {
                statusMessage.value = '网络错误: 无法连接到服务器'
            }
        }
        else if (error instanceof Error) {
            statusMessage.value = `请求错误: ${error.message}`
        }
        else {
            statusMessage.value = '请求错误: ' + String(error)
        }
    } finally {
        isSubmitting.value = false
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
    position: relative;
}

@keyframes highlight-fade {
    0% {
        /* 高亮状态 */
        background-color: #3498db;
        color: white;
    }
    100% {
        /* 原始状态 */
        background-color: #ecf0f1;
        color: #2c3e50;
    }
}

/* panel-header 高亮时的样式 */
.panel-header.highlighted {
    /* 立即应用高亮样式 */
    background: linear-gradient(90deg, 
        rgba(41, 98, 255, 0.3), 
        rgba(61, 126, 255, 0.4),
        var(--panel-bg)
    ) !important;
    border-bottom: 2px solid var(--primary) !important;
    
    /* 添加发光效果 */
    box-shadow: 0 5px 15px rgba(41, 98, 255, 0.4);
    
    /* 3秒淡出动画 */
    animation: header-highlight-fade 3s ease-out forwards;
}

/* 高亮淡出动画 */
@keyframes header-highlight-fade {
    0% {
        background: linear-gradient(90deg, 
            rgba(41, 98, 255, 0.3), 
            rgba(61, 126, 255, 0.4),
            var(--panel-bg)
        );
        border-bottom: 2px solid var(--primary);
        box-shadow: 0 5px 15px rgba(41, 98, 255, 0.4);
    }
    70% {
        background: linear-gradient(90deg, 
            rgba(41, 98, 255, 0.1), 
            rgba(61, 126, 255, 0.2),
            var(--panel-bg)
        );
        border-bottom: 1px solid color-mix(in srgb, var(--primary) 50%, var(--border));
        box-shadow: 0 2px 8px rgba(41, 98, 255, 0.2);
    }
    100% {
        background: linear-gradient(90deg, var(--darker-bg) 0%, var(--panel-bg) 100%);
        border-bottom: 1px solid var(--border);
        box-shadow: none;
    }
}

.submit-overlay {
    position: absolute;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background-color: rgba(0, 0, 0, 0.5);
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
    z-index: 100;
    border-radius: 8px;
}

/* 加载动画 */
.loading-spinner {
    width: 40px;
    height: 40px;
    border: 3px solid rgba(255, 255, 255, 0.3);
    border-radius: 50%;
    border-top-color: var(--primary);
    animation: spin 1s ease-in-out infinite;
    margin-bottom: 12px;
}

@keyframes spin {
    to { transform: rotate(360deg); }
}

/* 加载文本 */
.loading-text {
    color: white;
    font-size: 14px;
    font-weight: 500;
}

/* 禁用状态下的样式 */
.trade-panel.disabled {
    opacity: 0.7;
}

.trade-panel.disabled .form-select,
.trade-panel.disabled .form-input,
.trade-panel.disabled .submit-btn,
.trade-panel.disabled .reset-btn {
    cursor: not-allowed;
    opacity: 0.6;
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