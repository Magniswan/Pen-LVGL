import IndexComponent from './index.vue';
import { BasePage } from '../../base-page.js';

class PageIndex extends BasePage {
  onLoad(options) {
    super.onLoad(options);
    this.setRootComponent(IndexComponent);
  }
}

export default PageIndex;
