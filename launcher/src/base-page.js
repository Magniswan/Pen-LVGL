class ResourcePage extends $falcon.Page {
  constructor() {
    super();
    this.timeoutTokens = new Set();
    this.intervalTokens = new Set();
  }

  setTimeout(callback, delay) {
    const token = setTimeout(() => {
      this.timeoutTokens.delete(token);
      callback();
    }, delay);
    this.timeoutTokens.add(token);
    return token;
  }

  clearTimeout(token) {
    clearTimeout(token);
    this.timeoutTokens.delete(token);
  }

  setInterval(callback, delay) {
    const token = setInterval(callback, delay);
    this.intervalTokens.add(token);
    return token;
  }

  clearInterval(token) {
    clearInterval(token);
    this.intervalTokens.delete(token);
  }

  release() {
    this.timeoutTokens.forEach((token) => clearTimeout(token));
    this.intervalTokens.forEach((token) => clearInterval(token));
    this.timeoutTokens.clear();
    this.intervalTokens.clear();
  }
}

export class BasePage extends ResourcePage {
  onLoad(options) {
    super.onLoad(options);
    this.options = options || {};
  }

  onShow() {
    super.onShow();
    if (this.$root && this.$root.onShow) this.$root.onShow();
  }

  onHide() {
    super.onHide();
    if (this.$root && this.$root.onHide) this.$root.onHide();
  }

  onUnload() {
    try {
      if (this.$root && this.$root.onUnload) this.$root.onUnload();
      super.onUnload();
    } finally {
      this.release();
    }
  }
}
