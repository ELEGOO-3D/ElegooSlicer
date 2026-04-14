const PrinterManager = {
    setup() {
        const printerStore = usePrinterStore();
        return {
            printerStore
        };
    },
    data() {
        return {
            showAddPrinter: false,
            currentPrinter: null,
            statusUpdateInterval: null,
            // Show printer settings modal for discovered devices
            showPrinterAdvancedSettings: false,
            // Show printer settings modal for manual connection devices
            showPrinterSimpleSettings: false,
            licenseRefreshInterval: null,
        };
    },

    async mounted() {
        nativeIpc.on('onUserInfoUpdated', (data) => {
            console.log('Received user info update event from backend:', data);
            const oldUserId = this.printerStore.userInfo.userId;
            this.printerStore.userInfo = data;
            console.log('loginErrorMessage:', this.printerStore.userInfo.loginErrorMessage);

            // Check if userId changed and user is logged in (loginStatus === 1)
            if (data.userId && data.userId !== oldUserId && data.loginStatus === 1) {
                console.log('UserId changed and user is online, starting license refresh');
                this.startLicenseRefresh();
            } else if (!data.userId || data.loginStatus !== 1) {
                // User logged out or offline, stop license refresh and reset flag
                this.stopLicenseRefresh();
            }
        });
        await nativeIpc.request('ready', {});
        await this.init();
        await this.startLicenseRefresh();
        disableRightClickMenu();
    },

    watch: {
        //Actively request to discover printers when the add printer dialog is shown
        showAddPrinter: {
            handler(newVal) {
                if (newVal) {
                    if (this.printerStore.isDiscovering) {
                        return;
                    }
                    this.printerStore.requestDiscoverPrinters();
                }
            },
            immediate: true
        }
    },

    beforeUnmount() {
        this.printerStore.uninit();
        if (this.statusUpdateInterval) {
            clearInterval(this.statusUpdateInterval);
        }
        this.stopLicenseRefresh();
    },

    methods: {
        async init() {
            await this.printerStore.init();
        },

        // Returns HTML with the login keyword highlighted in primary blue
        getLoginToViewHtml() {
            const loginWord = `<span class="login-link" onclick="loginLinkClickHandler()">${this.$t("printerManager.login")}</span>`; 
            // loginToView is expected to contain a {0} placeholder for the login word
            const raw = this.$t("printerManager.loginToView", [loginWord]);
            return raw;
        },


        canShowProgressText(printerStatus, connectStatus) {
            return PrinterStatusUtils.canShowProgressText(printerStatus, connectStatus);
        },
        getPrinterStatus(printerStatus, connectStatus) {
            return PrinterStatusUtils.getPrinterStatus(printerStatus, connectStatus, this.$t);
        },

        getPrinterStatusStyle(printerStatus, connectStatus) {
            return PrinterStatusUtils.getPrinterStatusStyle(printerStatus, connectStatus);
        },

        shouldShowWarningIcon(printerStatus, connectStatus) {
            return PrinterStatusUtils.shouldShowWarningIcon(printerStatus, connectStatus);
        },

        getWarningTooltip(printerStatus) {
            return PrinterStatusUtils.getWarningTooltip(printerStatus, this.$t);
        },

        getPrinterProgress(printer) {
            return PrinterStatusUtils.getPrinterProgress(printer);
        },

        getPrinterRemainingTime(printer) {
            return PrinterStatusUtils.getPrinterRemainingTime(printer);
        },

        hasLicenseError(printer) {
            if (!printer || !printer.serialNumber || printer.networkType !== 1) {
                return false;
            }

            // printerStatus -1 means offline, no need to check license, connectStatus 0 means disconnected
            if(printer.printerStatus !== -1 && printer.connectStatus !== 0) {
                return false;
            }

            const status = this.printerStore.getLicenseStatusBySN(printer.serialNumber);
            return status !== null && status !== 1;
        },

        getLicenseErrorTooltip(printer) {
            if (!printer || !printer.serialNumber) {
                return '';
            }
            const status = this.printerStore.getLicenseStatusBySN(printer.serialNumber);
            return this.printerStore.getLicenseErrorMessage(status);
        },

        async startLicenseRefresh() {
            // Stop existing interval if any
            this.stopLicenseRefresh();

            if(!this.printerStore.userInfo.userId || this.printerStore.userInfo.loginStatus !== 1) {
                console.log('User not logged in or offline, not starting license refresh');
                return;
            }
            // Request immediately
            if (await this.refreshLicenseDevices()) {
                return;
            }
            // Start periodic refresh (every 20 seconds)
            this.licenseRefreshInterval = setInterval(async () => {
                await this.refreshLicenseDevices();
            }, 20 * 1000); // 20 seconds
        },

        stopLicenseRefresh() {
            if (this.licenseRefreshInterval) {
                clearInterval(this.licenseRefreshInterval);
                this.licenseRefreshInterval = null;
            }
        },

        async refreshLicenseDevices() {
            try {
                await this.printerStore.requestLicenseExpiredDevices();
                console.log('License expired devices refreshed successfully');
                this.stopLicenseRefresh();
                return true;
            } catch (error) {
                console.error('Failed to request license expired devices:', error);
                // Keep trying with interval
                return false;
            }
        },

        async handleRefresh() {
            const loading = ElLoading.service({
                lock: true,
            });
            try {
                await new Promise(resolve => setTimeout(resolve, 500));
                await this.printerStore.requestRefreshWanPrinters();
                await this.printerStore.requestPrinterList();
                // Refresh all printer status
                try {
                    await this.printerStore.refreshPrinterStatus();
                } catch (error) {
                    console.error('Failed to refresh printer status:', error);
                }
                this.startLicenseRefresh();
            } finally {
                loading.close();
            }
        },

        showAddPrinterModal() {
            this.showAddPrinter = true;
        },

        showPrinterSettingsByIndex(index) {
            if (index >= 0 && index < this.printerStore.printers.length) {
                this.currentPrinter = this.printerStore.printers[index];
                this.showPrinterAdvancedSettings = (this.currentPrinter && this.currentPrinter.isPhysicalPrinter) !== true;
                this.showPrinterSimpleSettings = !this.showPrinterAdvancedSettings;
            }
        },
        async showPrinterSettings(printer) {
            this.currentPrinter = printer;
            this.showPrinterAdvancedSettings = (this.currentPrinter && this.currentPrinter.isPhysicalPrinter) !== true;
            this.showPrinterSimpleSettings = !this.showPrinterAdvancedSettings;
        },

        async showPrinterDetail(printer) {
            // Check if license needs renewal before showing details
            if (await this.checkAndRenewLicense(printer)) {
                return; // License renewal in progress, don't show details
            }
            this.printerStore.showPrinterDetail(printer.printerId);
        },

        async checkAndRenewLicense(printer) {
            // Only check for network printers with license errors
            if (!this.hasLicenseError(printer)) {
                return false;
            }
            const status = this.printerStore.getLicenseStatusBySN(printer.serialNumber);
            if (status == 3) {
                // License already renewed to confirm, just show details
                try {
                    // Show success message
                    await DialogHelper.confirm({
                        title: this.$t('printerManager.info'),
                        message: this.$t('printerManager.licenseRenewToConfirm'),
                        confirmText: this.$t('printerManager.ok'),
                        showCancelButton: false,
                    });
                } catch (e) { }
                return true;
            }
            else if (status == 9) {
                try {
                    // Show restart message
                    await DialogHelper.confirm({
                        title: this.$t('printerManager.info'),
                        message: this.$t('printerManager.licenseNotFound'),
                        confirmText: this.$t('printerManager.ok'),
                        showCancelButton: false,
                    });
                } catch (e) { }
                return true;
            }
            try {
                // Show confirmation dialog
                await DialogHelper.confirm({
                    title: this.$t('printerManager.info'),
                    message: this.$t('printerManager.licenseRenewConfirmMessage'),
                    confirmText: this.$t('printerManager.continue'),
                    cancelText: this.$t('printerManager.cancel')
                });

                // User confirmed, start license renewal
                let loading = ElLoading.service({
                    lock: true,
                    text: this.$t('printerManager.licenseRenewing'),
                });

                await new Promise(resolve => setTimeout(resolve, 500)); // Small delay for better UX

                let retrying = true;
                while (retrying) {
                    try {
                        await this.printerStore.renewLicense(printer.serialNumber);

                        // Update local license status to 3 (RENEW_TO_CONFIRM)
                        this.printerStore.updateLicenseStatus(printer.serialNumber, 3);

                        loading.close();

                        try {
                            // Show success message
                            await DialogHelper.confirm({
                                title: this.$t('printerManager.info'),
                                message: this.$t('printerManager.licenseRenewSuccess'),
                                confirmText: this.$t('printerManager.ok'),
                                imageSrc: './img/success.svg',
                                showCancelButton: false,
                            });
                        } catch (e) { }

                        return true;
                    } catch (error) {
                        loading.close();

                        // Show error message with retry option
                        const errorCode = error.code || 'UNKNOWN';
                        try {
                            await DialogHelper.confirm({
                                title: this.$t('printerManager.info'),
                                message: this.$t('printerManager.licenseRenewFailed', [errorCode]),
                                confirmText: this.$t('printerManager.retry'),
                                cancelText: this.$t('printerManager.cancel'),
                                imageSrc: './img/error.svg'
                            });

                            // User clicked retry, restart loading and loop again
                            loading = ElLoading.service({
                                lock: true,
                                text: this.$t('printerManager.licenseRenewing'),
                            });
                            await new Promise(resolve => setTimeout(resolve, 500)); // Small delay before retry
                        } catch (cancelError) {
                            // User clicked cancel, exit retry loop
                            retrying = false;
                            return true;
                        }
                    }
                }
            } catch (error) {
                // User cancelled`
                return true;
            }
        },

        closeModals() {
            this.showAddPrinter = false;
            this.showPrinterAdvancedSettings = false;
            this.showPrinterSimpleSettings = false;
            this.currentPrinter = null;
        },
    }
};


window.loginLinkClickHandler = function () {
    // Implement the login logic here
    console.log("Login link clicked");
    try {
        nativeIpc.request("checkLoginStatus", {});
    } catch (error) {
        console.error('Check login status failed:', error);
    }
};


