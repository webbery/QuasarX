import { createApp } from 'vue'
import 'element-plus/dist/index.css'
import '@fortawesome/fontawesome-free/css/all.min.css'
import "@vue-flow/core/dist/style.css";
import "@vue-flow/core/dist/theme-default.css";
import App from './App.vue'
import axios from 'axios'
import { message } from './tool'
import { createPinia } from 'pinia';

axios.defaults.headers.common['Access-Control-Allow-Origin'] = process.env.VUE_APP_Access_Control_Allow_Origin
const app = createApp(App)
const pinia = createPinia();
app.config.globalProperties.$message = message
app.use(pinia)
    .mount('#app')
    .$nextTick(() => postMessage({ payload: 'removeLoading' }, '*'));
// for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
//     app.component(key, component)
// }
