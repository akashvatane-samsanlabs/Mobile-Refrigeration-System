import React from 'react';
import { Pressable, StyleSheet, Text, View } from 'react-native';

type Props = { children: React.ReactNode };
type State = { failed: boolean };

/** Catches render errors so a UI bug shows a recovery screen, not a crash. */
export class ErrorBoundary extends React.Component<Props, State> {
  state: State = { failed: false };

  static getDerivedStateFromError(): State {
    return { failed: true };
  }

  componentDidCatch(error: Error): void {
    console.error('UI error caught by ErrorBoundary:', error);
  }

  private reset = (): void => {
    this.setState({ failed: false });
  };

  render(): React.ReactNode {
    if (!this.state.failed) {
      return this.props.children;
    }

    return (
      <View style={styles.container}>
        <Text style={styles.title}>Something went wrong</Text>
        <Text style={styles.message}>
          The app hit an unexpected problem. Your device settings were not
          changed.
        </Text>
        <Pressable onPress={this.reset} style={styles.button}>
          <Text style={styles.buttonText}>Try again</Text>
        </Pressable>
      </View>
    );
  }
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    alignItems: 'center',
    justifyContent: 'center',
    padding: 24,
    backgroundColor: '#F4F7FB',
  },
  title: {
    fontSize: 20,
    fontWeight: '700',
    color: '#102A43',
    marginBottom: 8,
  },
  message: {
    fontSize: 14,
    color: '#486581',
    textAlign: 'center',
    marginBottom: 20,
  },
  button: {
    backgroundColor: '#0B5FFF',
    borderRadius: 10,
    paddingVertical: 12,
    paddingHorizontal: 24,
  },
  buttonText: {
    color: '#FFFFFF',
    fontWeight: '700',
    fontSize: 14,
  },
});
