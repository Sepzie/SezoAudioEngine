import { useEffect, useState } from 'react';
import { StyleSheet, Text, View } from 'react-native';
import { AudioEngineModule } from 'sezo-audio-engine';

export default function App() {
  const [status, setStatus] = useState('Idle');

  useEffect(() => {
    let mounted = true;
    AudioEngineModule.initialize({ sampleRate: 44100, maxTracks: 4 })
      .then(() => {
        if (mounted) {
          setStatus('Initialized');
        }
      })
      .catch(() => {
        if (mounted) {
          setStatus('Init failed');
        }
      });

    return () => {
      mounted = false;
      AudioEngineModule.release().catch(() => {});
    };
  }, []);

  return (
    <View style={styles.container}>
      <Text style={styles.title}>Sezo Audio Engine</Text>
      <Text style={styles.status}>Status: {status}</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    alignItems: 'center',
    justifyContent: 'center',
    backgroundColor: '#0b0b0b'
  },
  title: {
    fontSize: 24,
    color: '#f5f5f5'
  },
  status: {
    marginTop: 12,
    fontSize: 16,
    color: '#9aa0a6'
  }
});
