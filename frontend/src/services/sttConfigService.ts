/**
 * STT Configuration Service
 * Handles communication with backend for STT configuration management
 */

import { 
  STTConfig, 
  STTConfigMessage, 
  STTConfigMessageType, 
  ConfigValidationResult,
  ConfigChangeNotification,
  ConfigSchema,
  ConfigMetadata,
  STTConfigManagerOptions
} from '../types/sttConfig';

export class STTConfigService {
  private ws: WebSocket | null = null;
  private requestId = 0;
  private pendingRequests = new Map<string, {
    resolve: (value: any) => void;
    reject: (error: Error) => void;
  }>();
  private changeListeners: ((notification: ConfigChangeNotification) => void)[] = [];
  private options: STTConfigManagerOptions;

  constructor(options: STTConfigManagerOptions = {}) {
    this.options = {
      autoSave: true,
      validateOnUpdate: true,
      enableChangeNotifications: true,
      ...options
    };
  }

  /**
   * Initialize the service with WebSocket connection
   */
  async initialize(wsUrl: string): Promise<void> {
    return new Promise((resolve, reject) => {
      try {
        this.ws = new WebSocket(wsUrl);
        
        this.ws.onopen = () => {
          console.log('STT Config Service connected');
          resolve();
        };
        
        this.ws.onmessage = (event) => {
          this.handleMessage(event.data);
        };
        
        this.ws.onerror = (error) => {
          console.error('STT Config Service WebSocket error:', error);
          reject(new Error('WebSocket connection failed'));
        };
        
        this.ws.onclose = () => {
          console.log('STT Config Service disconnected');
          this.handleDisconnection();
        };
        
      } catch (error) {
        reject(error);
      }
    });
  }

  /**
   * Get current STT configuration
   */
  async getConfig(): Promise<STTConfig> {
    return this.sendRequest('GET_CONFIG', '');
  }

  /**
   * Update entire STT configuration
   */
  async updateConfig(config: STTConfig): Promise<ConfigValidationResult> {
    if (this.options.validateOnUpdate) {
      const validation = await this.validateConfig(config);
      if (!validation.isValid) {
        return validation;
      }
    }

    const result = await this.sendRequest('UPDATE_CONFIG', config);
    return {
      isValid: true,
      errors: [],
      warnings: result.warnings || []
    };
  }

  /**
   * Update specific configuration value
   */
  async updateConfigValue(section: string, key: string, value: any): Promise<ConfigValidationResult> {
    const data = { section, key, value: String(value) };
    const result = await this.sendRequest('UPDATE_CONFIG_VALUE', data);
    
    if (result.error) {
      return {
        isValid: false,
        errors: [result.error],
        warnings: []
      };
    }

    return {
      isValid: true,
      errors: [],
      warnings: result.warnings || []
    };
  }

  /**
   * Validate configuration
   */
  async validateConfig(config: STTConfig): Promise<ConfigValidationResult> {
    return this.sendRequest('VALIDATE_CONFIG', config);
  }

  /**
   * Reset configuration to defaults
   */
  async resetConfig(): Promise<STTConfig> {
    return this.sendRequest('RESET_CONFIG', '');
  }

  /**
   * Get configuration schema
   */
  async getConfigSchema(): Promise<ConfigSchema> {
    return this.sendRequest('GET_SCHEMA', '');
  }

  /**
   * Get configuration metadata
   */
  async getConfigMetadata(): Promise<ConfigMetadata> {
    return this.sendRequest('GET_METADATA', '');
  }

  /**
   * Get available Whisper models
   */
  async getAvailableModels(): Promise<string[]> {
    return this.sendRequest('GET_AVAILABLE_MODELS', '');
  }

  /**
   * Get supported quantization levels
   */
  async getSupportedQuantizationLevels(): Promise<string[]> {
    return this.sendRequest('GET_SUPPORTED_QUANTIZATION_LEVELS', '');
  }

  /**
   * Register listener for configuration changes
   */
  onConfigChange(listener: (notification: ConfigChangeNotification) => void): () => void {
    if (this.options.enableChangeNotifications) {
      this.changeListeners.push(listener);
      
      // Return unsubscribe function
      return () => {
        const index = this.changeListeners.indexOf(listener);
        if (index > -1) {
          this.changeListeners.splice(index, 1);
        }
      };
    }
    
    return () => {}; // No-op if notifications disabled
  }

  /**
   * Disconnect from the service
   */
  disconnect(): void {
    if (this.ws) {
      this.ws.close();
      this.ws = null;
    }
    
    // Reject all pending requests
    for (const [requestId, { reject }] of this.pendingRequests) {
      reject(new Error('Service disconnected'));
    }
    this.pendingRequests.clear();
  }

  /**
   * Check if service is connected
   */
  isConnected(): boolean {
    return this.ws?.readyState === WebSocket.OPEN;
  }

  private generateRequestId(): string {
    return `stt-config-${++this.requestId}-${Date.now()}`;
  }

  private async sendRequest(type: STTConfigMessageType, data: any): Promise<any> {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      throw new Error('WebSocket not connected');
    }

    const requestId = this.generateRequestId();
    const message: STTConfigMessage = {
      type,
      requestId,
      data
    };

    return new Promise((resolve, reject) => {
      this.pendingRequests.set(requestId, { resolve, reject });
      
      try {
        this.ws!.send(JSON.stringify(message));
        
        // Set timeout for request
        setTimeout(() => {
          if (this.pendingRequests.has(requestId)) {
            this.pendingRequests.delete(requestId);
            reject(new Error(`Request timeout for ${type}`));
          }
        }, 10000); // 10 second timeout
        
      } catch (error) {
        this.pendingRequests.delete(requestId);
        reject(error);
      }
    });
  }

  private handleMessage(data: string): void {
    try {
      const message: STTConfigMessage = JSON.parse(data);
      
      if (message.type === 'CONFIG_CHANGED') {
        this.handleConfigChange(message);
        return;
      }

      // Handle response to pending request
      if (message.requestId && this.pendingRequests.has(message.requestId)) {
        const { resolve, reject } = this.pendingRequests.get(message.requestId)!;
        this.pendingRequests.delete(message.requestId);

        if (message.success === false) {
          reject(new Error(message.error || 'Request failed'));
        } else {
          resolve(message.data);
        }
      }
      
    } catch (error) {
      console.error('Failed to parse STT config message:', error);
    }
  }

  private handleConfigChange(message: STTConfigMessage): void {
    if (!this.options.enableChangeNotifications) {
      return;
    }

    try {
      const notification: ConfigChangeNotification = message.data;
      
      // Notify all listeners
      for (const listener of this.changeListeners) {
        try {
          listener(notification);
        } catch (error) {
          console.error('Error in config change listener:', error);
        }
      }
      
    } catch (error) {
      console.error('Failed to handle config change notification:', error);
    }
  }

  private handleDisconnection(): void {
    // Reject all pending requests
    for (const [requestId, { reject }] of this.pendingRequests) {
      reject(new Error('Connection lost'));
    }
    this.pendingRequests.clear();

    // Attempt to reconnect after a delay
    setTimeout(() => {
      if (!this.isConnected()) {
        console.log('Attempting to reconnect STT Config Service...');
        // Note: In a real implementation, you would store the original URL
        // and attempt to reconnect here
      }
    }, 5000);
  }
}

// Singleton instance for global use
export const sttConfigService = new STTConfigService();