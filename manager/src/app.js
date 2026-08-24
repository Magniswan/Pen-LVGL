import { BasePage } from './base-page.js';

const DESIGN_WIDTH = 960;

class App extends $falcon.App {
  onLaunch(options) {
    super.onLaunch(options);
    this.setViewPort(DESIGN_WIDTH);
    $falcon.useDefaultBasePageClass(BasePage);
    console.log(`[lvgl-manager] launch viewport=${DESIGN_WIDTH}`);
  }
}

export default App;
