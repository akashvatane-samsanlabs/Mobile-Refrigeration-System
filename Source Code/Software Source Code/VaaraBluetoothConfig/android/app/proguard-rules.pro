# React Native's own rules are applied automatically by the Gradle plugin.

# Bluetooth Classic native module: classes are looked up by React Native by
# name when the module is registered, so they must survive code shrinking.
-keep class kjd.reactnative.** { *; }
-dontwarn kjd.reactnative.**
