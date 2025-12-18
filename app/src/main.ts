import { createApp } from 'vue'
import ElementPlus from 'element-plus'
import * as ElementPlusIconsVue from '@element-plus/icons-vue'
import 'element-plus/dist/index.css'
import '@fortawesome/fontawesome-free/css/all.min.css'
import "@vue-flow/core/dist/style.css";
import "@vue-flow/core/dist/theme-default.css";
import App from './App.vue'
import axios from 'axios'
import { message } from './tool'

axios.defaults.headers.common['Access-Control-Allow-Origin'] = process.env.VUE_APP_Access_Control_Allow_Origin
const app = createApp(App)
app.config.globalProperties.$message = message
app.use(ElementPlus)
    .mount('#app')
    .$nextTick(() => postMessage({ payload: 'removeLoading' }, '*'));
// for (const [key, component] of Object.entries(ElementPlusIconsVue)) {
//     app.component(key, component)
// }

if (process.env.NODE_ENV === "development") {
    import("autopreview/vue3").then(({ default: AutoPreview }) => {
        new AutoPreview("#app", (app) => {
            app.use(router).use(store).use(vuetify);
        });
    });
} else {
    
}
