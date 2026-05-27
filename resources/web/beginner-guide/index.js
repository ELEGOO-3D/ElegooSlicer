// Vue.js beginner guide application
const { createApp } = Vue;
const { ElButton, ElDialog, ElInput, ElSelect, ElOption, ElForm, ElFormItem, ElLoading } = ElementPlus;

const BeginnerGuideApp = {
    data() {
        return {
            isIniting: false,
            currentTutorial: 0, // 当前教程索引 (0, 1, 2)
            currentPage: 0 // 当前页面索引
        };
    },

    computed: {
        tutorials() {
            const t = this.$t.bind(this);
            return [
                {
                    title: t('tutorials.tutorial1.title'),
                    icon: './img/1.svg',
                    iconActive: './img/1-active.svg',
                    steps: [
                        {
                            image: './img/guide/' + defaultLanguage() + '/1-1.png',
                            description: t('tutorials.tutorial1.step1Desc')
                        },
                        {
                            image: './img/guide/' + defaultLanguage() + '/1-2.png',
                            description: t('tutorials.tutorial1.step2Desc')
                        },
                        {
                            image: './img/guide/' + defaultLanguage() + '/1-3.png',
                            description: t('tutorials.tutorial1.step3Desc')
                        },
                        {
                            image: './img/guide/' + defaultLanguage() + '/1-4.png',
                            description: t('tutorials.tutorial1.step4Desc')
                        }
                    ]
                },
                {
                    title: t('tutorials.tutorial2.title'),
                    icon: './img/2.svg',
                    iconActive: './img/2-active.svg',
                    steps: [
                        {
                            image: './img/guide/' + defaultLanguage() + '/2-1.png',
                            description: t('tutorials.tutorial2.step1Desc')
                        },
                        {
                            image: './img/guide/' + defaultLanguage() + '/2-2.png',
                            description: t('tutorials.tutorial2.step2Desc')
                        }
                    ]
                },
                {
                    title: t('tutorials.tutorial3.title'),
                    icon: './img/3.svg',
                    iconActive: './img/3-active.svg',
                    steps: [
                        {
                            image: './img/guide/' + defaultLanguage() + '/3-1.png',
                            description: t('tutorials.tutorial3.step1Desc')
                        },
                        {
                            image: './img/guide/' + defaultLanguage() + '/3-2.png',
                            description: t('tutorials.tutorial3.step2Desc')
                        }
                    ]
                }
            ];
        }
    },

    methods: {
        // 获取当前显示的图片
        getCurrentImage() {
            const tutorial = this.tutorials[this.currentTutorial];
            return tutorial.steps[this.currentPage].image;
        },

        // 获取当前步骤信息
        getCurrentStep() {
            const tutorial = this.tutorials[this.currentTutorial];
            return tutorial.steps[this.currentPage];
        },

        // 获取当前教程的图片总数
        getCurrentTutorialLength() {
            return this.tutorials[this.currentTutorial].steps.length;
        },

        // 切换教程
        switchTutorial(index) {
            this.currentTutorial = index;
            this.currentPage = 0; // 切换教程时重置到第一页
        },

        // 上一页
        prevPage() {
            if (this.currentPage > 0) {
                this.currentPage--;
            } else if (this.currentTutorial > 0) {
                // 如果是当前教程的第一页，跳到上一个教程的最后一页
                this.currentTutorial--;
                this.currentPage = this.getCurrentTutorialLength() - 1;
            }
        },

        // 下一页
        nextPage() {
            const maxPage = this.getCurrentTutorialLength() - 1;
            if (this.currentPage < maxPage) {
                this.currentPage++;
            } else if (this.currentTutorial < this.tutorials.length - 1) {
                // 如果是当前教程的最后一页，跳到下一个教程的第一页
                this.currentTutorial++;
                this.currentPage = 0;
            } else {
                // 如果是最后一页，关闭引导
                this.closeGuide();
            }
        },

        // 跳转到指定页
        goToPage(pageIndex) {
            this.currentPage = pageIndex;
        },

        // 判断能否上一页
        canGoPrev() {
            return this.currentTutorial > 0 || this.currentPage > 0;
        },

        // 判断能否下一页
        canGoNext() {
            return true; // 总是可以点击，最后一页会关闭
        },

        // 判断是否是最后一页
        isLastPage() {
            return this.currentTutorial === this.tutorials.length - 1 &&
                this.currentPage === this.getCurrentTutorialLength() - 1;
        },

        // 关闭引导
        closeGuide() {
            try {
                nativeIpc.sendEvent('closeDialog', {});
            } catch (error) {
                console.error('Failed to close dialog:', error);
            }
        },

        // IPC Communication methods
        async ipcRequest(method, params = {}, timeout = 10000) {
            try {
                const response = await nativeIpc.request(method, params, timeout);
                return response;
            } catch (error) {
                let message = `${error.message || 'Unknown error occurred'}`;
                // Show error notification using Element Plus message component
                if (window.ElementPlus && window.ElementPlus.ElMessage) {
                    window.ElementPlus.ElMessage.error({
                        message: message,
                        duration: 5000,
                        showClose: true
                    });
                }
                throw error;
            }
        },

        // Lifecycle methods
        async init() {
            this.isIniting = true;
            this.isIniting = false;
        },

        async sync() {

        },
    },
    mounted() {
        this.init();
    }
};

// Create and mount the Vue app
const app = createApp(BeginnerGuideApp)
    .use(ElementPlus);

// Use global i18n if available
if (typeof i18n !== 'undefined') {
    app.use(i18n);
}

app.mount('#app');
