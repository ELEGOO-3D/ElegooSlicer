const { createPinia, defineStore } = Pinia;

// Define a store
const usePrinterStore = defineStore('printer', {
  state: () => ({
    discoveredPrinters: [],
    isDiscovering: false,
    printers: [],
    printerModelList: null,
    statusUpdateInterval: null,
    licenseExpiredDevices: [],
    userInfo: {
      userId: null,
      nickname: null,
      email: null,
      phone: null,
      avatar: null,
      //login status, -1: no-user, 0: no-login, 1: logged in, other: offline
      loginStatus: -1,
      loginErrorMessage: null,
    }
  }),
  getters: {
    localPrinters: (state) => state.printers.filter(printer => printer.networkType === 0),
    networkPrinters: (state) => state.printers.filter(printer => printer.networkType === 1),
    userAvatar: (state) => {
      if (state.userInfo && state.userInfo.avatar) {
        return state.userInfo.avatar;
      } else {
        return "./img/default-avatar.svg";
      }
    },
    userName: (state) => {
      const loginStatus = state.userInfo ? state.userInfo.loginStatus : -1;
      if (loginStatus === -1) {
        return i18n.global.t('homepage.login');
      } else if (loginStatus === 1) {
        return state.userInfo.nickname || state.userInfo.email.split('@')[0] || state.userInfo.phone;
      }
      else {
        const text = state.userInfo.nickname || (state.userInfo.email && state.userInfo.email.split('@')[0]) || state.userInfo.phone;
        return text;
      }
    },
    loginStatusStyle(state) {
      const status = state.userInfo ? state.userInfo.loginStatus : -1;
      if (status === 1) {
        return { color: '#4FD22A' };
      } else {
        return { color: '#BBBBBB' };
      }
    },
    isLoggedIn: (state) => {
      return state.userInfo && state.userInfo.loginStatus !== -1;
    }
  },
  actions: {
    validateHost(rule, value, callback) {
      if (!value || value.trim().length === 0) {
        callback(new Error(i18n.global.t('printerSetting.pleaseEnterHostNameIpUrl')));
        return;
      }

      const trimmedValue = value.trim();
      const portPattern = /:([1-9]\d{0,3}|[1-5]\d{4}|6[0-4]\d{3}|65[0-4]\d{2}|655[0-2]\d|6553[0-5])$/;

      // Check IP address (IPv4 or IPv6) with optional port
      // IPv4: 192.168.1.1 or 192.168.1.1:8080
      const ipv4Match = trimmedValue.match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})(:([1-9]\d{0,3}|[1-5]\d{4}|6[0-4]\d{3}|65[0-4]\d{2}|655[0-2]\d|6553[0-5]))?$/);
      if (ipv4Match) {
        for (let i = 1; i <= 4; i++) {
          if (parseInt(ipv4Match[i], 10) > 255) {
            callback(new Error(i18n.global.t('printerSetting.pleaseEnterValidHostNameIpUrl')));
            return;
          }
        }
        callback();
        return;
      }

      // IPv6: [2001:db8::1] or [2001:db8::1]:8080 or 2001:db8::1
      const ipv6Match = trimmedValue.match(/^(\[([0-9a-fA-F:]+)\]|([0-9a-fA-F:]+))(:([1-9]\d{0,3}|[1-5]\d{4}|6[0-4]\d{3}|65[0-4]\d{2}|655[0-2]\d|6553[0-5]))?$/);
      if (ipv6Match) {
        const ip = ipv6Match[2] || ipv6Match[3];
        const segments = ip.split(':');
        if (segments.length <= 8 && segments.every(s => s === '' || /^[0-9a-fA-F]{1,4}$/i.test(s))) {
          callback();
          return;
        }
      }

      // Check URL
      if (/^https?:\/\/.+/.test(trimmedValue)) {
        try {
          new URL(trimmedValue);
          callback();
          return;
        } catch (e) {
          callback(new Error(i18n.global.t('printerSetting.pleaseEnterValidHostNameIpUrl')));
          return;
        }
      }

      // Check hostname with optional port
      if (/^[a-zA-Z0-9][a-zA-Z0-9\-]*[a-zA-Z0-9](:([1-9]\d{0,3}|[1-5]\d{4}|6[0-4]\d{3}|65[0-4]\d{2}|655[0-2]\d|6553[0-5]))?$/.test(trimmedValue)) {
        callback();
        return;
      }

      callback(new Error(i18n.global.t('printerSetting.pleaseEnterValidHostNameIpUrl')));
    },


    async init() {
      const loading = ElLoading.service({
        lock: true,
      });
      await new Promise(resolve => setTimeout(resolve, 500));
      await this.requestUserInfo();
      await this.requestPrinterModelList();
      await this.requestPrinterList();
      await this.startStatusUpdates();
      loading.close();
    },

    uninit() {
      // Clear status update interval
      if (this.statusUpdateInterval) {
        clearInterval(this.statusUpdateInterval);
        this.statusUpdateInterval = null;
      }
    },



    async ipcRequest(method, params, timeout = 10000) {
      try {
        const response = await nativeIpc.request(method, params, timeout);
        return response;
      } catch (error) {
        let message = ''
        // if (method === "request_add_printer" || method === "request_add_physical_printer") {

        //   if (params.printer.authMode === 'token' && (error.code === 200 || error.code === 202 || error.code === 203)) {
        //     message = i18n.global.t("printerAuth.failedToAddPrinterInvalidAccessCode");
        //   } else {
        //     message = i18n.global.t("printerAuth.failedToAddPrinter");
        //   }

        // } else {
        message = `${error.message || 'Unknown error occurred'}`
        // }
        // Show error notification using Element Plus message component
        // code === 5 means the request is cancelled
        if (error.code !== 5) {
          if (window.ElementPlus && window.ElementPlus.ElMessage) {
            window.ElementPlus.ElMessage.error({
              message: message,
              duration: 5000,
              showClose: true
            });
          }
        }
        throw error;
      }
    },

    async requestPrinterList() {
      try {
        const response = await this.ipcRequest('request_printer_list', {});
        // Update printers list and main client status
        if (response && typeof response === 'object') {
          this.printers = Array.isArray(response.printers) ? response.printers : [];
        }
      } catch (error) {
        console.error('Failed to request printer list:', error);
      }
    },

    async requestRefreshWanPrinters() {
      try {
        await this.ipcRequest('request_refresh_wan_printers', {});
      } catch (error) {
        console.error('Failed to request refresh wan printers:', error);
      }
    },

    startStatusUpdates() {
      // Clear any existing interval
      if (this.statusUpdateInterval) {
        clearInterval(this.statusUpdateInterval);
      }

      this.statusUpdateInterval = setInterval(async () => {
        try {
          await this.requestPrinterList();
        } catch (error) {
          console.error('Failed to update printer status:', error);
        }
      }, 3000);
    },

    stopStatusUpdates() {
      if (this.statusUpdateInterval) {
        clearInterval(this.statusUpdateInterval);
        this.statusUpdateInterval = null;
      }
    },

    async requestPrinterModelList() {
      try {
        const response = await this.ipcRequest('request_printer_model_list', {});
        this.printerModelList = response;
      } catch (error) {
        console.error('Failed to request printer model list:', error);
      }
    },

    async requestDiscoverPrinters() {
      this.isDiscovering = true;
      try {
        const response = await this.ipcRequest('request_discover_printers', {}, 60 * 1000);
        this.discoveredPrinters = response || [];
        this.isDiscovering = false;
      } catch (error) {
        console.error('Failed to discover printers:', error);
        this.isDiscovering = false;
      }
    },

    async requestDeletePrinter(printerId) {
      const loading = ElLoading.service({
        lock: true,
      });
      try {
        await this.ipcRequest('request_delete_printer', { printerId }, 60 * 1000);
        this.requestPrinterList();
        await new Promise(resolve => setTimeout(resolve, 500));
        // Response handling is done in event listeners
      } catch (error) {
        console.error('Failed to delete printer:', error);
        throw error;
      }
      finally {
        loading.close();
      }
    },

    async requestAddPrinter(printer, showLoading = true, timeout = 60 * 1000) {

      let loading;
      if (showLoading) {
        loading = ElLoading.service({
          lock: true,
        });
      }
      try {
        await new Promise(resolve => setTimeout(resolve, 500));
        await this.ipcRequest('request_add_printer', { printer }, timeout);
        this.requestPrinterList();
        ElementPlus.ElMessage.success({
          message: i18n.global.t("printerManager.addPrinterSuccess"),
          duration: 3000,
        });
      } catch (error) {
        console.error('Failed to add printer:', error);
        throw error;
      } finally {
        if (loading) {
          loading.close();
        }
      }
    },

    async requestAddPhysicalPrinter(printer, showLoading = true, timeout = 60 * 1000) {
      let loading;
      if (showLoading) {
        loading = ElLoading.service({
          lock: true,
        });
      }
      try {
        await new Promise(resolve => setTimeout(resolve, 500));
        await this.ipcRequest('request_add_physical_printer', { printer }, timeout);
        this.requestPrinterList();
      } catch (error) {
        console.error('Failed to add physical printer:', error);
        throw error;
      } finally {
        if (loading) {
          loading.close();
        }
      }
    },

    async requestCancelAddPrinter(printer) {
      const loading = ElLoading.service({
        lock: true,
      });
      try {
        await this.ipcRequest('request_cancel_add_printer', { printer }, 10 * 1000);
        console.log('Cancelled printer connection:', printer);
      } catch (error) {
        console.error('Failed to cancel add printer:', error);
        // Don't throw error - cancellation is best-effort
      }
      finally {
        loading.close();
      }
    },

    async requestUpdatePrinterNameAndHost(printerId, printerName, host) {
      // const loading = ElLoading.service({
      //   lock: true,
      // });
      try {

        if (isEmptyString(printerName) && isEmptyString(host)) {
          // Nothing to update
          return;
        }

        if (printerName) {
          await this.ipcRequest('request_update_printer_name', {
            printerId,
            printerName
          }, 60 * 1000);
        }

        if (host) {
          const loading = ElLoading.service({
            lock: true,
          });
          try {
            await this.ipcRequest('request_update_printer_host', {
              printerId,
              host
            }, 60 * 1000);
          } finally {
            loading.close();
          }
        }

        ElementPlus.ElMessage.success({
          message: i18n.global.t("printerManager.modifySuccess"),
          duration: 3000,
        });

      } catch (error) {
        console.error('Failed to update printer name:', error);
        throw error;
      } finally {
        // loading.close();
      }

    },

    async requestUpdatePrinterName(printerId, printerName) {
      await this.requestUpdatePrinterNameAndHost(printerId, printerName, null);

    },

    async requestUpdatePrinterHost(printerId, host) {
      await this.requestUpdatePrinterNameAndHost(printerId, null, host);
    },

    async requestUpdatePhysicalPrinter(printerId, printer) {
      const loading = ElLoading.service({
        lock: true,
      });
      try {
        await this.ipcRequest('request_update_physical_printer', { printerId, printer });
        this.requestPrinterList();
        ElementPlus.ElMessage.success({
          message: i18n.global.t("printerManager.modifySuccess"),
          duration: 3000,
        });
      } catch (error) {
        console.error('failed to update printer:', error);
        throw error;
      } finally {
        loading.close();
      }
    },
    // Handle printer information updates
    updatePrinterInfo(data) {
      if (data.command === 'update_printer_info') {
        this.loadPrinterList();
      }
    },

    async showPrinterDetail(printerId) {
      try {
        const response = await this.ipcRequest('request_printer_detail', { printerId });
        return response;
      } catch (error) {
        console.error('Failed to get printer detail:', error);
        return null;
      }
    },


    // Helper method for closing modals (to be implemented by UI components)
    closeModals() {
      // This method should be overridden or called from UI components
      // to handle modal closing logic
      console.log('closeModals called - should be handled by UI components');
    },

    async handleUserInfoClick() {
      console.log('User info clicked - implement login modal logic here');
      try {
        await this.ipcRequest('checkLoginStatus', {});
      } catch (error) {
        console.error('Check login status failed:', error);
      }
    },

    async requestUserInfo() {
      try {
        const response = await this.ipcRequest('request_user_info', {});
        this.userInfo = response || {
          userId: null,
          userName: null,
          email: null,
          phone: null,
          avatarUrl: null,
          loginStatus: -1,
          loginErrorMessage: null,
        };
      } catch (error) {
        console.error('Failed to request user info:', error);
      }
    },

    async requestLicenseExpiredDevices() {
      try {
        // backend ipc call to get license expired devices
        const response = await nativeIpc.request('get_license_expired_devices', {});
        if (response && response.devices) {
          this.licenseExpiredDevices = response.devices;
          console.log('License expired devices:', this.licenseExpiredDevices);
        }
      } catch (error) {
        console.error('Failed to request license expired devices:', error);
        this.licenseExpiredDevices = [];
        throw error;
      }
    },

    getLicenseStatusBySN(serialNumber) {

      if (!serialNumber || !this.licenseExpiredDevices || this.licenseExpiredDevices.length === 0) {
        return null;
      }
      const device = this.licenseExpiredDevices.find(d => d.serialNumber === serialNumber);
      return device ? device.status : null;
    },

    getLicenseErrorMessage(status) {
      const messages = {
        2: i18n.global.t('printerManager.licenseExpired'),
        3: i18n.global.t('printerManager.licenseRenewToConfirm'),
        9: i18n.global.t('printerManager.licenseNotFound')
      };
      return messages[status] || '';
    },

    async renewLicense(serialNumber) {
      try {
        const response = await this.ipcRequest('renew_license', { serialNumber });
        return response;
      } catch (error) {
        console.error('Failed to renew license:', error);
        throw error;
      }
    },

    async refreshPrinterStatus() {
      try {
        await this.ipcRequest('refresh_printer_status', {});
        console.log('Printer status refreshed successfully');
      } catch (error) {
        console.error('Failed to refresh printer status:', error);
        throw error;
      }
    },

    updateLicenseStatus(serialNumber, newStatus) {
      const device = this.licenseExpiredDevices.find(d => d.serialNumber === serialNumber);
      if (device) {
        device.status = newStatus;
      }
    }
  }

});