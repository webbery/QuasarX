<script setup lang="ts">
import { ref, computed, watch } from 'vue'
import axios from 'axios'

const props = defineProps<{ active?: boolean }>()

const emit = defineEmits<{
    'macro-data-click': [payload: any]
}>()

// ── 宏观数据状态 ──
const macroCountry = ref('usa')
const macroIndicator = ref('')
const macroStartDate = ref('')
const macroEndDate = ref('')
const macroDownloading = ref(false)
const macroStatus = ref('')
const macroDownloadExpanded = ref(true)

// 宏观数据列表
const macroDataSummary = ref<any[]>([])  // 列表数据（每个指标的摘要）
const loadingMacroData = ref(false)
const selectedMacroKey = ref('')  // 当前选中的指标 key

// 指标元数据（与 useMacroIndicators.ts 保持一致）
const INDICATOR_META: Record<string, string> = {
    cpi: 'CPI',
    ppi: 'PPI',
    gdp: 'GDP',
    pmi: 'PMI',
    m2: 'M2',
    social_financing: '社融',
    unemployment: '失业率',
    trade: '贸易',
    interest_rate: '利率',
    retail_sales: '零售销售',
    industrial_production: '工业增加值',
    fixed_asset_investment: '固投',
    consumer_confidence: '消费者信心',
    housing_starts: '新屋开工',
    nonfarm: '非农就业',
    sofr: 'SOFR',
    treasury_yield: '国债收益率',
    treasury_spread: '国债利差',
    fx_rate: '汇率',
}

const FRED_INDICATORS = new Set(['sofr', 'treasury_yield', 'treasury_spread', 'fx_rate'])
const COMMON_INDICATORS = [
    'cpi', 'ppi', 'gdp', 'pmi', 'm2', 'social_financing',
    'unemployment', 'trade', 'interest_rate', 'retail_sales',
    'industrial_production', 'fixed_asset_investment',
    'consumer_confidence', 'housing_starts', 'nonfarm'
]

const COUNTRY_META: Record<string, string> = {
    china: '中国',
    usa: '美国',
    global: '全球'
}

// 根据选择的国家过滤指标选项
const filteredMacroOptions = computed(() => {
    const options: { indicator: string; label: string }[] = []
    const country = macroCountry.value

    // 通用指标：所有国家都显示
    for (const indicator of COMMON_INDICATORS) {
        options.push({
            indicator,
            label: INDICATOR_META[indicator]
        })
    }

    // FRED 指标：仅美国
    if (country === 'usa') {
        for (const indicator of FRED_INDICATORS) {
            options.push({
                indicator,
                label: INDICATOR_META[indicator]
            })
        }
    }

    return options
})

// 切换国家时清空已选指标
const onMacroCountryChange = () => {
    macroIndicator.value = ''
}

// 下载宏观数据
const onDownloadMacro = async () => {
    if (!macroIndicator.value) {
        macroStatus.value = '请选择指标'
        return
    }

    macroDownloading.value = true
    macroStatus.value = ''
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        const params: any = {
            country: macroCountry.value,
            indicator: macroIndicator.value
        }
        if (macroStartDate.value) params.start = macroStartDate.value
        if (macroEndDate.value) params.end = macroEndDate.value

        const response = await axios.get(`https://${server}/v0/market/macro`, {
            params,
            headers: { 'Authorization': token || '' }
        })

        const data = response.data
        if (data.status === 'success') {
            const count = data.count || 0
            macroStatus.value = `下载成功：${count} 条数据`
            // 更新列表数据
            await loadMacroDataList()
        } else {
            macroStatus.value = `下载失败：${data.message || '未知错误'}`
        }
    } catch (err: any) {
        macroStatus.value = `下载失败：${err.response?.data?.message || err.message}`
    } finally {
        macroDownloading.value = false
    }
}

// 强制刷新（绕过缓存）
const onRefreshMacro = async () => {
    if (!macroIndicator.value) {
        macroStatus.value = '请选择指标'
        return
    }

    macroDownloading.value = true
    macroStatus.value = ''
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        const params: any = {
            country: macroCountry.value,
            indicator: macroIndicator.value,
            refresh: 'true'
        }
        if (macroStartDate.value) params.start = macroStartDate.value
        if (macroEndDate.value) params.end = macroEndDate.value

        const response = await axios.get(`https://${server}/v0/market/macro`, {
            params,
            headers: { 'Authorization': token || '' }
        })

        const data = response.data
        if (data.status === 'success') {
            const count = data.count || 0
            macroStatus.value = `刷新成功：${count} 条数据`
            // 更新列表数据
            await loadMacroDataList()
        } else {
            macroStatus.value = `刷新失败：${data.message || '未知错误'}`
        }
    } catch (err: any) {
        macroStatus.value = `刷新失败：${err.response?.data?.message || err.message}`
    } finally {
        macroDownloading.value = false
    }
}

// 加载宏观数据列表（遍历所有指标检查缓存）
const loadMacroDataList = async () => {
    loadingMacroData.value = true
    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')

    try {
        const countries = ['china', 'usa', 'global']
        const allIndicators = [...COMMON_INDICATORS, ...Array.from(FRED_INDICATORS)]
        const summaryList: any[] = []

        for (const country of countries) {
            const indicators = country === 'usa' ? allIndicators : COMMON_INDICATORS
            for (const indicator of indicators) {
                try {
                    const response = await axios.get(`https://${server}/v0/market/macro`, {
                        params: { country, indicator },
                        headers: { 'Authorization': token || '' }
                    })

                    const data = response.data
                    if (data.status === 'success' && data.count > 0) {
                        // 计算起始和结束日期
                        const dates = data.data?.map((d: any) => d.date).filter(Boolean) || []
                        const startDate = dates.length > 0 ? dates[0] : ''
                        const endDate = dates.length > 0 ? dates[dates.length - 1] : ''

                        summaryList.push({
                            country,
                            indicator,
                            country_name: COUNTRY_META[country] || country,
                            indicator_name: INDICATOR_META[indicator] || indicator,
                            count: data.count,
                            start_date: startDate,
                            end_date: endDate,
                            updated_at: data.updated_at || '',
                            data: data.data || []  // 保留完整数据用于详情展示
                        })
                    }
                } catch (err) {
                    // 忽略错误（可能是缓存不存在）
                }
            }
        }

        macroDataSummary.value = summaryList
    } catch (err: any) {
        console.error('加载宏观数据列表失败:', err)
    } finally {
        loadingMacroData.value = false
    }
}

// 单击指标行，emit 到 App.vue 显示详情
const onMacroRowClick = (item: any) => {
    selectedMacroKey.value = `${item.country}/${item.indicator}`
    // emit 事件到 App.vue，传递指标数据
    emit('macro-data-click', {
        country: item.country,
        country_name: item.country_name,
        indicator: item.indicator,
        indicator_name: item.indicator_name,
        data: item.data,
        count: item.count,
        start_date: item.start_date,
        end_date: item.end_date
    })
}

// 切换指标时自动加载列表
watch(macroIndicator, async (newIndicator) => {
    if (newIndicator && macroDataSummary.value.length === 0) {
        // 首次选择指标时加载列表
        await loadMacroDataList()
    }
})

// 切换到宏观数据 tab 时自动加载列表
watch(() => props.active, (active) => {
    if (active && macroDataSummary.value.length === 0) {
        loadMacroDataList()
    }
})

</script>

<template>
    <div class="tab-content">
        <div class="section-title">
            <i class="fas fa-globe"></i> 宏观数据管理
        </div>

        <!-- 下载区 -->
        <div class="download-section">
            <div class="section-header" @click="macroDownloadExpanded = !macroDownloadExpanded">
                <div class="section-title">
                    <i class="fas fa-download"></i> 数据下载
                </div>
                <i class="fas chevron-icon" :class="macroDownloadExpanded ? 'fa-chevron-up' : 'fa-chevron-down'"></i>
            </div>
            <div v-show="macroDownloadExpanded" class="download-body">
                <div class="download-card">
                    <div class="card-title"><i class="fas fa-chart-area"></i> 下载宏观指标</div>
                    <div class="input-row">
                        <div class="input-group">
                            <label>国家</label>
                            <select v-model="macroCountry" class="quote-select" @change="onMacroCountryChange">
                                <option value="china">中国</option>
                                <option value="usa">美国</option>
                                <option value="global">全球</option>
                            </select>
                        </div>
                        <div class="input-group">
                            <label>指标</label>
                            <select v-model="macroIndicator" class="quote-select">
                                <option value="">请选择指标</option>
                                <option v-for="opt in filteredMacroOptions" :key="opt.indicator" :value="opt.indicator">
                                    {{ opt.label }}
                                </option>
                            </select>
                        </div>
                    </div>
                    <div class="input-row">
                        <div class="input-group">
                            <label>开始日期 (可选)</label>
                            <input type="date" v-model="macroStartDate" />
                        </div>
                        <div class="input-group">
                            <label>结束日期 (可选)</label>
                            <input type="date" v-model="macroEndDate" />
                        </div>
                    </div>
                    <div class="button-row">
                        <button class="btn btn-primary" @click="onDownloadMacro" :disabled="macroDownloading || !macroIndicator">
                            <i class="fas fa-download"></i>
                            {{ macroDownloading ? '下载中...' : '下载数据' }}
                        </button>
                        <button class="btn" @click="onRefreshMacro" :disabled="macroDownloading || !macroIndicator">
                            <i class="fas fa-sync-alt"></i> 强制刷新
                        </button>
                    </div>
                    <div v-if="macroStatus" class="status-text" :class="{ 'status-error': macroStatus.includes('失败') }">
                        {{ macroStatus }}
                    </div>
                </div>
            </div>
        </div>

        <!-- 宏观数据列表 -->
        <div class="manage-section">
            <div class="section-title">
                <i class="fas fa-database"></i> 宏观数据
            </div>

            <div v-if="loadingMacroData" class="loading-text">
                <i class="fas fa-spinner fa-spin"></i> 加载中...
            </div>
            <div v-else-if="macroDataSummary.length === 0" class="empty-text">
                <i class="fas fa-info-circle"></i> 暂无宏观数据，请先下载
            </div>
            <div v-else class="quote-data-table">
                <div class="table-scroll">
                    <table class="data-table">
                        <thead class="sticky-header">
                            <tr>
                                <th>国家</th>
                                <th>指标</th>
                                <th>起始日期</th>
                                <th>结束日期</th>
                                <th>数据量</th>
                                <th>更新时间</th>
                            </tr>
                        </thead>
                        <tbody>
                            <tr v-for="item in macroDataSummary" :key="`${item.country}/${item.indicator}`"
                                :class="{ 'row-selected': selectedMacroKey === `${item.country}/${item.indicator}` }"
                                @click="onMacroRowClick(item)"
                                style="cursor: pointer;">
                                <td>{{ item.country_name }}</td>
                                <td>{{ item.indicator_name }}</td>
                                <td>{{ item.start_date || '-' }}</td>
                                <td>{{ item.end_date || '-' }}</td>
                                <td>{{ item.count?.toLocaleString() || '-' }}</td>
                                <td>{{ item.updated_at || '-' }}</td>
                            </tr>
                        </tbody>
                    </table>
                </div>
            </div>
        </div>
    </div>
</template>

<style scoped>
@import './datacenter.scss';
</style>
