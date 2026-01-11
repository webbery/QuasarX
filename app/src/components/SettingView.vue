<template>
    <div>
        <div class="expand-all">
            <button @click="toggleAllSections">{{ allExpanded ? '收起所有' : '展开所有' }}</button>
        </div>
        
        <div class="settings-panel">
            <!-- 添加远程服务器 -->
            <div class="settings-section">
                <div class="section-header" @click="toggleSection('server')">
                    <div class="section-title">
                        <div class="section-icon" style="background-color: rgba(52, 152, 219, 0.1); color: #3498db;">
                            <span>🌐</span>
                        </div>
                        <span>添加/删除远程服务器</span>
                    </div>
                    <div class="section-arrow" :class="{ rotated: expandedSections.server }">▼</div>
                </div>
                <div class="section-content" :class="{ expanded: expandedSections.server }">
                    <div class="form-group">
                        <label>服务器地址</label>
                        <input type="text" class="form-control" v-model="serverForm.address" placeholder="例如:192.168.1.100:8080 或 example.com">
                    </div>
                    <div class="form-group">
                        <label>别名</label>
                        <input type="text" class="form-control" v-model="serverForm.name" placeholder="别名">
                    </div>
                    <div class="form-group" v-if="serverForm.authMethod === 'password'">
                        <label>密码</label>
                        <input type="password" class="form-control" v-model="serverForm.password" placeholder="密码" style="margin-top: 10px;">
                    </div>
                    <button class="btn btn-primary" @click="addServer">添加服务器</button>
                </div>
            </div>
            <!-- 股票设置 -->
            <div class="settings-section">
                <div class="section-header" @click="toggleSection('stock')">
                    <div class="section-title">
                        <div class="section-icon" style="background-color: rgba(46, 204, 113, 0.1); color: #2ecc71;">
                            <span>📈</span>
                        </div>
                        <span>股票设置</span>
                    </div>
                    <div class="section-arrow" :class="{ rotated: expandedSections.stock }">▼</div>
                </div>
                <div class="section-content" :class="{ expanded: expandedSections.stock }">
                    <div class="form-group">
                        <label>每日下单上限</label>
                        <input type="number" class="form-control" v-model="stockSettings.dailyOrderLimit" placeholder="例如：1000">
                        <label>每秒下单上限</label>
                        <input type="number" class="form-control" v-model="stockSettings.perSecondOrderLimit" placeholder="例如：10">
                        <label>每秒撤单上限</label>
                        <input type="number" class="form-control" v-model="stockSettings.perSecondCancelLimit" placeholder="例如：5">
                    </div>
                    <button class="btn btn-primary" @click="updateStockSettings"
                        :disabled="updateStockSettingsLoading" :class="{ 'btn-loading': updateStockSettingsLoading }">
                        {{ updateStockSettingsLoading ? '更新中...' : '更新股票设置' }}
                    </button>
                </div>
            </div>
            <div class="settings-section">
                <div class="section-header" @click="toggleSection('fund')">
                    <div class="section-title">
                        <div class="section-icon" style="background-color: rgba(241, 196, 15, 0.1); color: #f1c40f;">
                            <span>💰</span>
                        </div>
                        <span>设置资金账号</span>
                    </div>
                    <div class="section-arrow" :class="{ rotated: expandedSections.fund }">▼</div>
                </div>
                <div class="section-content" :class="{ expanded: expandedSections.fund }">
                    <div class="form-group">
                        <label>账号</label>
                        <input type="text" class="form-control" v-model="fundForm.account" placeholder="请输入资金账号">
                        <label>密码</label>
                        <input type="password" class="form-control" v-model="fundForm.password" placeholder="请输入资金账号密码">
                    </div>
                    <button class="btn btn-primary" @click="updateFundAccount">更新资金账号</button>
                </div>
            </div>
            
            <!-- 设置SMTP -->
            <div class="settings-section">
                <div class="section-header" @click="toggleSection('smtp')">
                    <div class="section-title">
                        <div class="section-icon" style="background-color: rgba(231, 76, 60, 0.1); color: #e74c3c;">
                            <span>📧</span>
                        </div>
                        <span>设置SMTP</span>
                    </div>
                    <div class="section-arrow" :class="{ rotated: expandedSections.smtp }">▼</div>
                </div>
                <div class="section-content" :class="{ expanded: expandedSections.smtp }">
                    <div class="form-group">
                        <label>SMTP服务器</label>
                        <input type="text" class="form-control" v-model="smtpConfig.host" placeholder="例如：smtp.gmail.com">
                    </div>
                    <div class="form-group">
                        <label>端口</label>
                        <input type="number" class="form-control" v-model="smtpConfig.port" placeholder="例如：465">
                    </div>
                    <div class="form-group">
                        <label>用户名</label>
                        <input type="text" class="form-control" v-model="smtpConfig.username" placeholder="SMTP用户名">
                    </div>
                    <div class="form-group">
                        <label>密码</label>
                        <input type="password" class="form-control" v-model="smtpConfig.password" placeholder="SMTP密码">
                    </div>
                    <div class="form-group">
                        <label>加密方式</label>
                        <select class="form-control" v-model="smtpConfig.encryption">
                            <option value="ssl">SSL</option>
                            <option value="tls">TLS</option>
                            <option value="none">无</option>
                        </select>
                    </div>
                    <button class="btn btn-primary" @click="updateSmtp">更新SMTP设置</button>
                </div>
            </div>
            
            <!-- 设置每日任务执行时间点 -->
            <div class="settings-section">
                <div class="section-header" @click="toggleSection('task')">
                    <div class="section-title">
                        <div class="section-icon" style="background-color: rgba(26, 188, 156, 0.1); color: #1abc9c;">
                            <span>⏰</span>
                        </div>
                        <span>设置每日任务执行时间点</span>
                    </div>
                    <div class="section-arrow" :class="{ rotated: expandedSections.task }">▼</div>
                </div>
                <div class="section-content" :class="{ expanded: expandedSections.task }">
                    <!-- <div class="form-group">
                        <label>任务类型</label>
                        <select class="form-control" v-model="selectedTask">
                            <option v-for="task in tasks" :value="task">{{ task.name }}</option>
                        </select>
                    </div> -->
                    <div class="form-group">
                        <label>执行时间</label>
                        <div class="time-selector">
                            <select v-model="selectedTask.hour">
                                <option v-for="h in 24" :value="h-1">{{ String(h-1).padStart(2, '0') }}</option>
                            </select>
                            <span>:</span>
                            <select v-model="selectedTask.minute">
                                <option v-for="m in 60" :value="m-1">{{ String(m-1).padStart(2, '0') }}</option>
                            </select>
                        </div>
                    </div>
                    <div class="form-group">
                        <label>
                            <input type="checkbox" v-model="selectedTask.enabled"> 启用此任务
                        </label>
                    </div>
                    <button class="btn btn-primary" @click="updateTaskSchedule">更新任务计划</button>
                </div>
            </div>
        </div>
    </div>
</template>
<script setup>
import axios from 'axios';
import { ref, computed } from 'vue'
import Store from 'electron-store';
import { message } from '@/tool'

const store = new Store();
// 展开/收缩状态管理
const expandedSections = ref({
    server: false,
    password: false,
    exchange: false,
    fee: false,
    smtp: false,
    risk: false,
    fund: false,
    task: false
});

// 远程服务器数据
const serverForm = ref({
    address: '',
    name: '',
    port: 19107,
    authMethod: 'password',
    username: 'admin',
    password: '',
    privateKey: ''
});
 // 股票设置数据
const stockSettings = ref({
    dailyOrderLimit: 1000,
    perSecondOrderLimit: 20,
    perSecondCancelLimit: 5
});
const servers = ref(store.get('servers'));

// 密码修改数据
const passwordForm = ref({
    current: '',
    new: '',
    confirm: ''
});

// 交易所数据
const exchangePlatforms = {'华鑫证券': 'hx', '中泰证券': 'xtp', '上期交易所': 'ctp'};
const exchangeForm = ref({
    name: '',
    platform: Object.keys(exchangePlatforms)[0],
    account: '',
    password: '',
    quoteAddr: '',
    tradeAddr: '',
    validTime: '',
    apiKey: '',
    secretKey: ''
});

const fundForm = ref({
    account: '',
    password: ''
})

// SMTP配置
const smtpConfig = ref({
    host: '',
    port: 465,
    username: '',
    password: '',
    encryption: 'ssl'
});

// 任务调度
const tasks = ref([
    { name: '数据备份', hour: 2, minute: 0, enabled: true },
    { name: '生成报告', hour: 8, minute: 30, enabled: true },
    { name: '清理缓存', hour: 23, minute: 0, enabled: false }
]);

const selectedTask = ref(tasks.value[0]);

// 加载状态
const updateStockSettingsLoading = ref(false);

// 计算属性：检查是否所有部分都已展开
const allExpanded = computed(() => {
    return Object.values(expandedSections.value).every(val => val);
});

// 方法
const toggleSection = (section) => {
    expandedSections.value[section] = !expandedSections.value[section];
};

const toggleAllSections = () => {
    const shouldExpand = !allExpanded.value;
    for (const key in expandedSections.value) {
        expandedSections.value[key] = shouldExpand;
    }
};

 // 股票设置更新方法
const updateStockSettings = async () => {
    // 这里可以添加验证逻辑
    if (stockSettings.value.dailyOrderLimit < 0 || 
        stockSettings.value.perSecondOrderLimit < 0 || 
        stockSettings.value.perSecondCancelLimit < 0) {
        message.error(`数值不能为负数！`)
        return;
    }
    updateStockSettingsLoading.value = true;
    try {
        const response = await axios.put('/v0/stocks/params', {
            "order_limit": stockSettings.value.perSecondOrderLimit,
            "daily_limit": stockSettings.value.dailyOrderLimit,
            "cancel_limit": stockSettings.value.perSecondCancelLimit
        })
        if (response.status == 200) {
            message.success('股票设置已更新！');
        }
    } catch (err) {
        message.error(`设置异常 ${err.message}`)
    }
    updateStockSettingsLoading.value = false;
};
const addServer = () => {
    if (!serverForm.value.address) {
        alert('地址不能为空');
        return;
    }
    if (!serverForm.value.password) {
        alert('密码不能为空');
        return;
    }
    if (!serverForm.value.name) {
        alert('名字不能为空');
        return;
    }
    if (!servers.value) {
        servers.value = []
    }
    servers.value.push({
        address: serverForm.value.address,
        name: serverForm.value.name,
        passwd: serverForm.value.password
    });
    store.set('servers', servers.value)
};

const updatePassword = () => {
    if (passwordForm.value.new === passwordForm.value.confirm) {
        alert('密码更新成功!');
        passwordForm.value = { current: '', new: '', confirm: '' };
    } else {
        alert('新密码和确认密码不匹配!');
    }
};

const addExchange = async () => {
    let params = {
        'account': exchangeForm.value.account,
        'passwd': exchangeForm.value.password,
        'name': exchangeForm.value.name,
        'api': exchangePlatforms[exchangeForm.value.platform],
        'utc_active': exchangeForm.value.validTime
    }
    console.info(`已添加 ${params} 交易所账号`)
    const res = await axios.post('/server/config', params)
};

const updateFundAccount = async () => {
    console.info('TODO:更新资金账号')
}

const updateFeeRate = () => {
    alert('费率更新成功!');
};

const updateSmtp = () => {
    alert('SMTP设置已更新!');
};

const updateRiskFreeRate = () => {
    alert('无风险利率已更新!');
};

const updateTaskSchedule = () => {
    alert('任务计划已更新!');
};
</script>
<style scoped>
.expand-all {
    text-align: right;
    margin-bottom: 15px;
}

.expand-all button {
    background: rgba(41, 98, 255, 0.1);
    border: 1px solid var(--border);
    color: var(--primary);
    cursor: pointer;
    font-size: 0.9rem;
    padding: 6px 12px;
    border-radius: 6px;
    transition: all 0.2s;
}

.expand-all button:hover {
    background: rgba(41, 98, 255, 0.2);
}
.settings-panel {
    background: var(--panel-bg);
    border-radius: 10px;
    border: 1px solid var(--border);
    overflow: hidden;
}

.settings-section {
    border-bottom: 1px solid var(--border);
}

.settings-section:last-child {
    border-bottom: none;
}

.section-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 18px 24px;
    cursor: pointer;
    transition: background-color 0.3s;
}

.section-header:hover {
    background-color: rgba(41, 98, 255, 0.05);
}

.section-title {
    display: flex;
    align-items: center;
    font-size: 1.1rem;
    font-weight: 500;
    color: var(--text);
}

.section-icon {
    width: 38px;
    height: 38px;
    border-radius: 8px;
    display: flex;
    align-items: center;
    justify-content: center;
    margin-right: 15px;
    font-size: 18px;
    background: rgba(41, 98, 255, 0.1);
    color: var(--primary);
}

.section-arrow {
    transition: transform 0.3s;
    color: var(--text-secondary);
}

.section-arrow.rotated {
    transform: rotate(180deg);
}

.section-content {
    padding: 0 24px;
    max-height: 0;
    overflow: hidden;
    transition: all 0.4s ease;
}

.section-content.expanded {
    padding: 0 24px 24px;
    max-height: 400px; /* 限制最大高度 */
    overflow-y: auto; /* 添加垂直滚动 */
}

.form-group {
    margin-bottom: 18px;
    display: flex;
    align-items: center;
}

.form-group label {
    min-width: 140px;
    font-weight: 500;
    color: var(--text);
    margin-right: 15px;
    text-align: right;
}

.form-control {
    flex: 1;
    padding: 10px 14px;
    background: var(--darker-bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    font-size: 14px;
    color: var(--text);
    transition: border-color 0.3s;
}

.form-control:focus {
    outline: none;
    border-color: var(--primary);
    box-shadow: 0 0 0 2px rgba(41, 98, 255, 0.2);
}

textarea.form-control {
    min-height: 100px;
    resize: vertical;
}

.server-list {
    margin-top: 15px;
    max-height: 200px;
    overflow-y: auto;
}

.server-item {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 10px;
    background: var(--darker-bg);
    border-radius: 6px;
    margin-bottom: 8px;
    border: 1px solid var(--border);
}
.time-selector {
    display: flex;
    align-items: center;
}

.time-selector select {
    margin: 0 5px;
    padding: 8px;
    background: var(--darker-bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    color: var(--text);
}
@media (max-width: 768px) {
    .form-group {
        flex-direction: column;
        align-items: flex-start;
    }
    
    .form-group label {
        min-width: auto;
        text-align: left;
        margin-right: 0;
        margin-bottom: 8px;
    }
    .server-list {
        margin-left: 0;
    }
    
    .section-header {
        padding: 15px 20px;
    }
    
    .section-content {
        padding: 0 20px;
    }
    
    .section-content.expanded {
        padding: 0 20px 20px;
    }
}
</style>