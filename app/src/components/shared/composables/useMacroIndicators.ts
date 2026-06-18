/**
 * 宏观指标选择器 Composable
 * 提供 MacroHandler 支持的国家/指标组合，供波动率分析/信号分析面板使用
 */
import { ref, computed } from 'vue'

export interface MacroIndicatorOption {
  id: string            // 格式: "china/cpi"
  label: string         // 显示文本: "中国 / CPI"
  country: string
  indicator: string
  name: string          // 中文全称
}

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
  nonfarm: '非农就业'
}

const COUNTRY_META: Record<string, string> = {
  china: '中国',
  usa: '美国',
  global: '全球'
}

const VALID_INDICATORS = [
  'cpi', 'ppi', 'gdp', 'pmi', 'm2', 'social_financing',
  'unemployment', 'trade', 'interest_rate', 'retail_sales',
  'industrial_production', 'fixed_asset_investment',
  'consumer_confidence', 'housing_starts', 'nonfarm'
]

const VALID_COUNTRIES = ['china', 'usa', 'global']

export function useMacroIndicators() {
  /** 所有宏观指标选项（按国家分组） */
  const macroOptions = computed<MacroIndicatorOption[]>(() => {
    const options: MacroIndicatorOption[] = []
    for (const country of VALID_COUNTRIES) {
      for (const indicator of VALID_INDICATORS) {
        options.push({
          id: `${country}/${indicator}`,
          label: `${COUNTRY_META[country]} / ${INDICATOR_META[indicator]}`,
          country,
          indicator,
          name: `${COUNTRY_META[country]} ${INDICATOR_META[indicator]}`
        })
      }
    }
    return options
  })

  /** 按国家分组的选项 */
  const macroOptionsByCountry = computed(() => {
    const result: Record<string, MacroIndicatorOption[]> = {}
    for (const opt of macroOptions.value) {
      if (!result[opt.country]) result[opt.country] = []
      result[opt.country].push(opt)
    }
    return result
  })

  /** 判断是否为宏观指标 ID */
  function isMacroId(id: string): boolean {
    return VALID_COUNTRIES.some(c => id.startsWith(`${c}/`))
  }

  /** 解析宏观指标 ID */
  function parseMacroId(id: string): { country: string; indicator: string } | null {
    const parts = id.split('/')
    if (parts.length !== 2) return null
    const [country, indicator] = parts
    if (!VALID_COUNTRIES.includes(country)) return null
    if (!VALID_INDICATORS.includes(indicator)) return null
    return { country, indicator }
  }

  return {
    macroOptions,
    macroOptionsByCountry,
    isMacroId,
    parseMacroId
  }
}
