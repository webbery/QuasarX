import { createApp } from 'vue'
import './styles/main.scss'
import 'element-plus/dist/index.css'
import '@fortawesome/fontawesome-free/css/all.min.css'
import "@vue-flow/core/dist/style.css";
import "@vue-flow/core/dist/theme-default.css";
import "@vue-flow/controls/dist/style.css";
import "@vue-flow/minimap/dist/style.css";
import App from './App.vue'
import axios from 'axios'
import { message } from './tool'
import { createPinia } from 'pinia';
import { useChatStore } from './stores/chatStore';

axios.defaults.headers.common['Access-Control-Allow-Origin'] = process.env.VUE_APP_Access_Control_Allow_Origin
const app = createApp(App)
const pinia = createPinia();
app.config.globalProperties.$message = message
app.use(pinia)

// 启动时隐藏浮窗；会话与分支从 localStorage 恢复（不再清空历史）
const chatStore = useChatStore()
chatStore.visible = false

app.mount('#app')
    .$nextTick(() => postMessage({ payload: 'removeLoading' }, '*'));
// for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
//     app.component(key, component)
// }
