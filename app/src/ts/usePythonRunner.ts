import { ref, readonly, type Ref } from 'vue'
import { fetchEventSource } from '@microsoft/fetch-event-source'

export interface PythonRunnerOptions {
  script: string
  args?: string[]
  env?: string
}

export interface PythonRunnerResult {
  output: string[]
  errors: string[]
  exitCode: number | null
}

export function usePythonRunner() {
  const isRunning = ref(false)
  const outputLines: Ref<string[]> = ref([])
  const errorLines: Ref<string[]> = ref([])
  const exitCode: Ref<number | null> = ref(null)

  let abortController: AbortController | null = null

  async function run(options: PythonRunnerOptions, callbacks?: {
    onOutput?: (line: string) => void
    onError?: (line: string) => void
    onDone?: (code: number) => void
  }) {
    if (isRunning.value) {
      console.warn('[PythonRunner] already running')
      return
    }

    // 重置状态
    outputLines.value = []
    errorLines.value = []
    exitCode.value = null
    isRunning.value = true

    const server = localStorage.getItem('remote')
    const token = localStorage.getItem('token')
    const url = `https://${server}/v0/python/run`

    abortController = new AbortController()

    try {
      await fetchEventSource(url, {
        method: 'POST',
        headers: {
          'Authorization': token || '',
          'Content-Type': 'application/json',
        },
        body: JSON.stringify({
          script: options.script,
          args: options.args || [],
          env: options.env || '',
        }),
        signal: abortController.signal,

        onopen: async (response) => {
          if (response.ok) return
          throw new Error(`HTTP ${response.status}`)
        },

        onmessage: (event) => {
          if (!event.data) return
          const data = JSON.parse(event.data)

          switch (event.event) {
            case 'output':
              outputLines.value.push(data.line)
              callbacks?.onOutput?.(data.line)
              break
            case 'error':
              errorLines.value.push(data.line)
              callbacks?.onError?.(data.line)
              break
            case 'done':
              exitCode.value = data.exit_code
              callbacks?.onDone?.(data.exit_code)
              break
          }
        },

        onclose: () => {
          isRunning.value = false
        },

        onerror: (err) => {
          isRunning.value = false
          throw err
        },
      })
    } catch (e: any) {
      isRunning.value = false
      if (e.name !== 'AbortError') {
        console.error('[PythonRunner] failed:', e)
      }
    }
  }

  function abort() {
    abortController?.abort()
    isRunning.value = false
  }

  return {
    isRunning: readonly(isRunning),
    outputLines: readonly(outputLines),
    errorLines: readonly(errorLines),
    exitCode: readonly(exitCode),
    run,
    abort,
  }
}
