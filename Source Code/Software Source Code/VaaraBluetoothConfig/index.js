/**
 * @format
 */

import { AppRegistry } from 'react-native';
import App from './App';
import { name as appName } from './app.json';

// Non-fatal JS errors (e.g. from Bluetooth callbacks) are logged instead of
// interrupting the user; fatal ones still go to the default handler.
const defaultHandler = global.ErrorUtils?.getGlobalHandler?.();
global.ErrorUtils?.setGlobalHandler?.((error, isFatal) => {
  console.error('Unhandled JS error:', error);
  if (isFatal && defaultHandler) {
    defaultHandler(error, isFatal);
  }
});

AppRegistry.registerComponent(appName, () => App);
