import { createApp } from 'vue'
import 'element-plus/dist/index.css'
import '@fortawesome/fontawesome-free/css/all.min.css'
import "@vue-flow/core/dist/style.css";
import "@vue-flow/core/dist/theme-default.css";
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

// 启动时清空聊天历史并隐藏聊天框
const chatStore = useChatStore()
chatStore.clearMessages()
chatStore.visible = false

app.mount('#app')
    .$nextTick(() => postMessage({ payload: 'removeLoading' }, '*'));
// for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
//     app.component(key, component)
// }
