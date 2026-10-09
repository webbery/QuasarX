import axios from 'axios'
import type { HMMConfig, HMMResult } from './useHMMState'

interface TrainResponse {
  model: {
    A: number[][]
    mu: number[][]
    cov_diag: number[][]
    state_duration: number[]
  }
  training_info: {
    log_likelihood_history: number[]
    // 列名限定名（"sh.600111.return_1d"），顺序与 model.mu 的列一一对应
    features: string[]
  }
}

interface DecodeResponse {
  decode: {
    state_sequence: number[]
    probs: number[][]
    dates: string[]
  }
}

// 后端 isSafeName 只放行 alnum / _ / -，策略名来自 IndexedDB，不能直接透传
function safeStrategyId(id: string): string {
  const cleaned = id.replace(/[^A-Za-z0-9_-]/g, '_')
  return cleaned || 'default'
}

export function useHMMData() {
  /**
   * HMM 分析 = 两次请求：train 只回模型参数，逐日状态序列要拿 model_path 再走 decode。
   * 两次调用各自会重跑一次 buildObservations（重新加载行情算特征），故耗时约翻倍。
   *
   * 多标的走的是后端的面板 HMM：观测矩阵列 = 标的数 × 特征数，
   * 行 = 所有标的都有报价的日期交集，少于 30 行后端会 400。
   * 无数据的标的在 buildObservations 里被 continue 跳过，不会让整个请求失败。
   */
  async function fetchHMMAnalysis(
    symbols: string[],
    strategyId: string,
    startDate: string,
    endDate: string,
    frequency: string,
    params: HMMConfig
  ): Promise<HMMResult | null> {
    if (symbols.length === 0) return null

    try {
      const observationParams = {
        symbols,
        start: startDate,
        end: endDate,
        freq: frequency,
      }

      const trainRes = await axios.post<TrainResponse>('/v0/hmm', {
        action: 'train',
        ...observationParams,
        strategy_id: safeStrategyId(strategyId),
        features: params.features,
        n_states: params.n_states,
        max_iter: params.max_iter,
        tol: params.tol,
        regularization: params.regularization,
      })

      // decode 不传 features：后端会强制使用模型训练时的特征，传了不一致反而 400
      const decodeRes = await axios.post<DecodeResponse>('/v0/hmm', {
        action: 'decode',
        ...observationParams,
        model_path: trainRes.data.model_path,
      })

      const { model, training_info } = trainRes.data
      const { state_sequence, probs, dates } = decodeRes.data.decode

      return {
        states: state_sequence,
        dates,
        probs,
        transition: model.A,
        duration: model.state_duration,
        log_likelihood: training_info.log_likelihood_history,
        state_means: model.mu,
        state_covs: model.cov_diag,
        feature_names: training_info.features,
      }
    } catch (e) {
      console.error('[useHMMData] fetch error:', e)
      return null
    }
  }

  return {
    fetchHMMAnalysis
  }
}
