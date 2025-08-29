/**
 * STT Configuration Panel Component
 * Provides a UI for managing STT configuration settings
 */

import React, { useState, useEffect, useCallback } from 'react';
import { STTConfig, ConfigValidationResult, ConfigChangeNotification } from '../types/sttConfig';
import { sttConfigService } from '../services/sttConfigService';

interface STTConfigPanelProps {
  onConfigChange?: (config: STTConfig) => void;
  className?: string;
}

export const STTConfigPanel: React.FC<STTConfigPanelProps> = ({ 
  onConfigChange, 
  className = '' 
}) => {
  const [config, setConfig] = useState<STTConfig | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [validationResult, setValidationResult] = useState<ConfigValidationResult | null>(null);
  const [availableModels, setAvailableModels] = useState<string[]>([]);
  const [quantizationLevels, setQuantizationLevels] = useState<string[]>([]);
  const [isConnected, setIsConnected] = useState(false);

  // Initialize service and load configuration
  useEffect(() => {
    const initializeService = async () => {
      try {
        setLoading(true);
        setError(null);

        // Initialize WebSocket connection
        await sttConfigService.initialize('ws://localhost:8080');
        setIsConnected(true);

        // Load current configuration
        const currentConfig = await sttConfigService.getConfig();
        setConfig(currentConfig);

        // Load available options
        const [models, levels] = await Promise.all([
          sttConfigService.getAvailableModels(),
          sttConfigService.getSupportedQuantizationLevels()
        ]);
        
        setAvailableModels(models);
        setQuantizationLevels(levels);

        if (onConfigChange) {
          onConfigChange(currentConfig);
        }

      } catch (err) {
        setError(err instanceof Error ? err.message : 'Failed to initialize STT configuration');
        setIsConnected(false);
      } finally {
        setLoading(false);
      }
    };

    initializeService();

    // Register for configuration changes
    const unsubscribe = sttConfigService.onConfigChange((notification: ConfigChangeNotification) => {
      console.log('Configuration changed:', notification);
      setConfig(notification.config);
      
      if (onConfigChange) {
        onConfigChange(notification.config);
      }
    });

    return () => {
      unsubscribe();
      sttConfigService.disconnect();
    };
  }, [onConfigChange]);

  const handleConfigValueChange = useCallback(async (
    section: string, 
    key: string, 
    value: any
  ) => {
    if (!config) return;

    try {
      setError(null);
      setValidationResult(null);

      const result = await sttConfigService.updateConfigValue(section, key, value);
      
      if (!result.isValid) {
        setValidationResult(result);
        setError(result.errors.join(', '));
      } else {
        // Update local config state
        const updatedConfig = { ...config };
        const sectionObj = updatedConfig[section as keyof STTConfig] as any;
        if (sectionObj && typeof sectionObj === 'object') {
          sectionObj[key] = value;
          setConfig(updatedConfig);
        }

        if (result.warnings.length > 0) {
          setValidationResult(result);
        }
      }

    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to update configuration');
    }
  }, [config]);

  const handleResetConfig = useCallback(async () => {
    try {
      setError(null);
      setValidationResult(null);
      
      const defaultConfig = await sttConfigService.resetConfig();
      setConfig(defaultConfig);
      
      if (onConfigChange) {
        onConfigChange(defaultConfig);
      }
      
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Failed to reset configuration');
    }
  }, [onConfigChange]);

  if (loading) {
    return (
      <div className={`stt-config-panel ${className}`}>
        <div className="loading">Loading STT configuration...</div>
      </div>
    );
  }

  if (error && !config) {
    return (
      <div className={`stt-config-panel ${className}`}>
        <div className="error">
          <h3>Configuration Error</h3>
          <p>{error}</p>
          <button onClick={() => window.location.reload()}>Retry</button>
        </div>
      </div>
    );
  }

  if (!config) {
    return (
      <div className={`stt-config-panel ${className}`}>
        <div className="no-config">No configuration available</div>
      </div>
    );
  }

  return (
    <div className={`stt-config-panel ${className}`}>
      <div className="config-header">
        <h2>STT Configuration</h2>
        <div className="connection-status">
          <span className={`status-indicator ${isConnected ? 'connected' : 'disconnected'}`}>
            {isConnected ? '● Connected' : '● Disconnected'}
          </span>
        </div>
      </div>

      {error && (
        <div className="error-message">
          <strong>Error:</strong> {error}
        </div>
      )}

      {validationResult && validationResult.warnings.length > 0 && (
        <div className="warning-message">
          <strong>Warnings:</strong>
          <ul>
            {validationResult.warnings.map((warning, index) => (
              <li key={index}>{warning}</li>
            ))}
          </ul>
        </div>
      )}

      <div className="config-sections">
        {/* Model Configuration */}
        <div className="config-section">
          <h3>Model Settings</h3>
          
          <div className="config-field">
            <label htmlFor="defaultModel">Default Model:</label>
            <select
              id="defaultModel"
              value={config.model.defaultModel}
              onChange={(e) => handleConfigValueChange('model', 'defaultModel', e.target.value)}
            >
              {availableModels.length > 0 ? (
                availableModels.map(model => (
                  <option key={model} value={model}>{model}</option>
                ))
              ) : (
                ['tiny', 'base', 'small', 'medium', 'large'].map(model => (
                  <option key={model} value={model}>{model}</option>
                ))
              )}
            </select>
          </div>

          <div className="config-field">
            <label htmlFor="language">Language:</label>
            <input
              id="language"
              type="text"
              value={config.model.language}
              onChange={(e) => handleConfigValueChange('model', 'language', e.target.value)}
              placeholder="auto"
            />
          </div>

          <div className="config-field">
            <label>
              <input
                type="checkbox"
                checked={config.model.translateToEnglish}
                onChange={(e) => handleConfigValueChange('model', 'translateToEnglish', e.target.checked)}
              />
              Translate to English
            </label>
          </div>
        </div>

        {/* Language Detection */}
        <div className="config-section">
          <h3>Language Detection</h3>
          
          <div className="config-field">
            <label>
              <input
                type="checkbox"
                checked={config.languageDetection.enabled}
                onChange={(e) => handleConfigValueChange('languageDetection', 'enabled', e.target.checked)}
              />
              Enable Language Detection
            </label>
          </div>

          <div className="config-field">
            <label htmlFor="detectionThreshold">Detection Threshold:</label>
            <input
              id="detectionThreshold"
              type="range"
              min="0"
              max="1"
              step="0.1"
              value={config.languageDetection.threshold}
              onChange={(e) => handleConfigValueChange('languageDetection', 'threshold', parseFloat(e.target.value))}
            />
            <span>{config.languageDetection.threshold}</span>
          </div>

          <div className="config-field">
            <label>
              <input
                type="checkbox"
                checked={config.languageDetection.autoSwitching}
                onChange={(e) => handleConfigValueChange('languageDetection', 'autoSwitching', e.target.checked)}
              />
              Auto Language Switching
            </label>
          </div>
        </div>

        {/* Quantization */}
        <div className="config-section">
          <h3>Quantization</h3>
          
          <div className="config-field">
            <label htmlFor="quantizationLevel">Quantization Level:</label>
            <select
              id="quantizationLevel"
              value={config.quantization.level}
              onChange={(e) => handleConfigValueChange('quantization', 'level', e.target.value)}
            >
              {quantizationLevels.length > 0 ? (
                quantizationLevels.map(level => (
                  <option key={level} value={level}>{level}</option>
                ))
              ) : (
                ['FP32', 'FP16', 'INT8', 'AUTO'].map(level => (
                  <option key={level} value={level}>{level}</option>
                ))
              )}
            </select>
          </div>

          <div className="config-field">
            <label>
              <input
                type="checkbox"
                checked={config.quantization.enableGPUAcceleration}
                onChange={(e) => handleConfigValueChange('quantization', 'enableGPUAcceleration', e.target.checked)}
              />
              Enable GPU Acceleration
            </label>
          </div>

          <div className="config-field">
            <label htmlFor="accuracyThreshold">Accuracy Threshold:</label>
            <input
              id="accuracyThreshold"
              type="range"
              min="0"
              max="1"
              step="0.05"
              value={config.quantization.accuracyThreshold}
              onChange={(e) => handleConfigValueChange('quantization', 'accuracyThreshold', parseFloat(e.target.value))}
            />
            <span>{config.quantization.accuracyThreshold}</span>
          </div>
        </div>

        {/* Streaming */}
        <div className="config-section">
          <h3>Streaming</h3>
          
          <div className="config-field">
            <label>
              <input
                type="checkbox"
                checked={config.streaming.partialResultsEnabled}
                onChange={(e) => handleConfigValueChange('streaming', 'partialResultsEnabled', e.target.checked)}
              />
              Enable Partial Results
            </label>
          </div>

          <div className="config-field">
            <label htmlFor="minChunkSize">Min Chunk Size (ms):</label>
            <input
              id="minChunkSize"
              type="number"
              min="100"
              max="10000"
              value={config.streaming.minChunkSizeMs}
              onChange={(e) => handleConfigValueChange('streaming', 'minChunkSizeMs', parseInt(e.target.value))}
            />
          </div>

          <div className="config-field">
            <label htmlFor="maxChunkSize">Max Chunk Size (ms):</label>
            <input
              id="maxChunkSize"
              type="number"
              min="1000"
              max="30000"
              value={config.streaming.maxChunkSizeMs}
              onChange={(e) => handleConfigValueChange('streaming', 'maxChunkSizeMs', parseInt(e.target.value))}
            />
          </div>
        </div>

        {/* Confidence */}
        <div className="config-section">
          <h3>Confidence & Quality</h3>
          
          <div className="config-field">
            <label htmlFor="confidenceThreshold">Confidence Threshold:</label>
            <input
              id="confidenceThreshold"
              type="range"
              min="0"
              max="1"
              step="0.05"
              value={config.confidence.threshold}
              onChange={(e) => handleConfigValueChange('confidence', 'threshold', parseFloat(e.target.value))}
            />
            <span>{config.confidence.threshold}</span>
          </div>

          <div className="config-field">
            <label>
              <input
                type="checkbox"
                checked={config.confidence.wordLevelEnabled}
                onChange={(e) => handleConfigValueChange('confidence', 'wordLevelEnabled', e.target.checked)}
              />
              Word-level Confidence
            </label>
          </div>

          <div className="config-field">
            <label>
              <input
                type="checkbox"
                checked={config.confidence.qualityIndicatorsEnabled}
                onChange={(e) => handleConfigValueChange('confidence', 'qualityIndicatorsEnabled', e.target.checked)}
              />
              Quality Indicators
            </label>
          </div>
        </div>

        {/* Performance */}
        <div className="config-section">
          <h3>Performance</h3>
          
          <div className="config-field">
            <label htmlFor="threadCount">Thread Count:</label>
            <input
              id="threadCount"
              type="number"
              min="1"
              max="16"
              value={config.performance.threadCount}
              onChange={(e) => handleConfigValueChange('performance', 'threadCount', parseInt(e.target.value))}
            />
          </div>

          <div className="config-field">
            <label htmlFor="temperature">Temperature:</label>
            <input
              id="temperature"
              type="range"
              min="0"
              max="1"
              step="0.1"
              value={config.performance.temperature}
              onChange={(e) => handleConfigValueChange('performance', 'temperature', parseFloat(e.target.value))}
            />
            <span>{config.performance.temperature}</span>
          </div>
        </div>
      </div>

      <div className="config-actions">
        <button 
          onClick={handleResetConfig}
          className="reset-button"
        >
          Reset to Defaults
        </button>
      </div>
    </div>
  );
};

export default STTConfigPanel;