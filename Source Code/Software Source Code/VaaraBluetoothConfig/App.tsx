import React from 'react';
import { StatusBar, StyleSheet, View } from 'react-native';

import { ErrorBoundary } from './src/components/ErrorBoundary';
import { ConfigScreen } from './src/screens/ConfigScreen';

function App(): React.JSX.Element {
  return (
    <View style={styles.root}>
      <StatusBar barStyle="dark-content" />
      <ErrorBoundary>
        <ConfigScreen />
      </ErrorBoundary>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {
    flex: 1,
    backgroundColor: '#F4F7FB',
  },
});

export default App;
