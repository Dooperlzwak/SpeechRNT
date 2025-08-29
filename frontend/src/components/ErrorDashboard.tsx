/**
 * ErrorDashboard - Development tool for monitoring error metrics and system health
 * 
 * This component provides a comprehensive view of error handling performance,
 * recovery statistics, and system health metrics for debugging and optimization.
 */

import { useState, useEffect } from 'react';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from './ui/card';
import { Button } from './ui/button';
import { Badge } from './ui/badge';
import { 
  AlertTriangle, 
  CheckCircle, 
  Clock, 
  Activity,
  RefreshCw,
  Download
} from 'lucide-react';
import { useErrorHandler } from '../hooks/useErrorHandler';


interface ErrorDashboardProps {
  className?: string;
}

/**
 * Comprehensive error monitoring dashboard
 */
export function ErrorDashboard({ className }: ErrorDashboardProps) {
  const { getErrorMetrics } = useErrorHandler();
  const [metrics, setMetrics] = useState<any>(null);
  const [refreshInterval, setRefreshInterval] = useState<NodeJS.Timeout | null>(null);
  const [autoRefresh, setAutoRefresh] = useState(true);

  // Load metrics
  const loadMetrics = () => {
    const currentMetrics = getErrorMetrics();
    setMetrics(currentMetrics);
  };

  // Set up auto-refresh
  useEffect(() => {
    loadMetrics();

    if (autoRefresh) {
      const interval = setInterval(loadMetrics, 5000); // Refresh every 5 seconds
      setRefreshInterval(interval);
      return () => clearInterval(interval);
    } else if (refreshInterval) {
      clearInterval(refreshInterval);
      setRefreshInterval(null);
    }
  }, [autoRefresh]);

  // Export metrics as JSON
  const exportMetrics = () => {
    if (!metrics) return;
    
    const dataStr = JSON.stringify(metrics, null, 2);
    const dataBlob = new Blob([dataStr], { type: 'application/json' });
    const url = URL.createObjectURL(dataBlob);
    
    const link = document.createElement('a');
    link.href = url;
    link.download = `error-metrics-${new Date().toISOString().split('T')[0]}.json`;
    document.body.appendChild(link);
    link.click();
    document.body.removeChild(link);
    
    URL.revokeObjectURL(url);
  };

  if (!metrics) {
    return (
      <div className="flex items-center justify-center p-8">
        <div className="text-center">
          <RefreshCw className="h-8 w-8 animate-spin mx-auto mb-2" />
          <p>Loading error metrics...</p>
        </div>
      </div>
    );
  }

  const { recovery, comprehensive } = metrics;

  return (
    <div className={className}>
      <div className="flex items-center justify-between mb-6">
        <div>
          <h2 className="text-2xl font-bold">Error Dashboard</h2>
          <p className="text-gray-600">System error monitoring and analytics</p>
        </div>
        
        <div className="flex items-center gap-2">
          <Button
            variant="outline"
            size="sm"
            onClick={() => setAutoRefresh(!autoRefresh)}
          >
            <Activity className={`h-4 w-4 mr-2 ${autoRefresh ? 'animate-pulse' : ''}`} />
            {autoRefresh ? 'Auto' : 'Manual'}
          </Button>
          
          <Button variant="outline" size="sm" onClick={loadMetrics}>
            <RefreshCw className="h-4 w-4 mr-2" />
            Refresh
          </Button>
          
          <Button variant="outline" size="sm" onClick={exportMetrics}>
            <Download className="h-4 w-4 mr-2" />
            Export
          </Button>
        </div>
      </div>

      <div className="space-y-6">
        {/* Overview Section */}
        <div>
          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4">
            {/* System Health */}
            <Card>
              <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                <CardTitle className="text-sm font-medium">System Health</CardTitle>
                <Activity className="h-4 w-4 text-muted-foreground" />
              </CardHeader>
              <CardContent>
                <div className="text-2xl font-bold">
                  <Badge 
                    variant={
                      comprehensive.systemHealth.connectionQuality === 'good' ? 'default' :
                      comprehensive.systemHealth.connectionQuality === 'poor' ? 'secondary' : 'destructive'
                    }
                  >
                    {comprehensive.systemHealth.connectionQuality}
                  </Badge>
                </div>
                <p className="text-xs text-muted-foreground">
                  Error Rate: {comprehensive.systemHealth.currentErrorRate}/min
                </p>
              </CardContent>
            </Card>

            {/* Total Errors */}
            <Card>
              <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                <CardTitle className="text-sm font-medium">Total Errors</CardTitle>
                <AlertTriangle className="h-4 w-4 text-muted-foreground" />
              </CardHeader>
              <CardContent>
                <div className="text-2xl font-bold">{comprehensive.totalErrors}</div>
                <p className="text-xs text-muted-foreground">
                  Session: {comprehensive.sessionMetrics.totalErrors}
                </p>
              </CardContent>
            </Card>

            {/* Recovery Rate */}
            <Card>
              <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                <CardTitle className="text-sm font-medium">Recovery Rate</CardTitle>
                <CheckCircle className="h-4 w-4 text-muted-foreground" />
              </CardHeader>
              <CardContent>
                <div className="text-2xl font-bold">
                  {(comprehensive.recoverySuccessRate * 100).toFixed(1)}%
                </div>
                <p className="text-xs text-muted-foreground">
                  Current: {(comprehensive.systemHealth.currentRecoveryRate * 100).toFixed(1)}%
                </p>
              </CardContent>
            </Card>

            {/* Avg Recovery Time */}
            <Card>
              <CardHeader className="flex flex-row items-center justify-between space-y-0 pb-2">
                <CardTitle className="text-sm font-medium">Avg Recovery Time</CardTitle>
                <Clock className="h-4 w-4 text-muted-foreground" />
              </CardHeader>
              <CardContent>
                <div className="text-2xl font-bold">
                  {(comprehensive.averageRecoveryTime / 1000).toFixed(1)}s
                </div>
                <p className="text-xs text-muted-foreground">
                  User Interaction: {(comprehensive.userInteractionRate * 100).toFixed(1)}%
                </p>
              </CardContent>
            </Card>
          </div>

          {/* Error Types Chart */}
          <Card>
            <CardHeader>
              <CardTitle>Error Distribution</CardTitle>
              <CardDescription>Errors by type in current session</CardDescription>
            </CardHeader>
            <CardContent>
              <div className="space-y-2">
                {Object.entries(comprehensive.errorsByType).map(([type, count]) => (
                  <div key={type} className="flex items-center justify-between">
                    <div className="flex items-center gap-2">
                      <Badge variant="outline">{type}</Badge>
                    </div>
                    <div className="flex items-center gap-2">
                      <div className="w-32 bg-gray-200 rounded-full h-2">
                        <div 
                          className="bg-blue-600 h-2 rounded-full" 
                          style={{ 
                            width: `${((count as number) / comprehensive.totalErrors) * 100}%` 
                          }}
                        />
                      </div>
                      <span className="text-sm font-medium w-8 text-right">{count as number}</span>
                    </div>
                  </div>
                ))}
              </div>
            </CardContent>
          </Card>

          {/* Error Categories */}
          <div className="grid grid-cols-1 md:grid-cols-2 gap-4">
            <Card>
              <CardHeader>
                <CardTitle>Error Categories</CardTitle>
                <CardDescription>Distribution by error category</CardDescription>
              </CardHeader>
              <CardContent>
                <div className="space-y-3">
                  {Object.entries(comprehensive.errorsByCategory).map(([category, count]) => (
                    <div key={category} className="flex items-center justify-between">
                      <span className="capitalize">{category}</span>
                      <Badge variant="secondary">{count as number}</Badge>
                    </div>
                  ))}
                </div>
              </CardContent>
            </Card>

            <Card>
              <CardHeader>
                <CardTitle>Recovery Statistics</CardTitle>
                <CardDescription>Recovery performance metrics</CardDescription>
              </CardHeader>
              <CardContent>
                <div className="space-y-3">
                  <div className="flex items-center justify-between">
                    <span>Recovery Attempts</span>
                    <Badge>{recovery.totalRecoveryAttempts}</Badge>
                  </div>
                  <div className="flex items-center justify-between">
                    <span>Success Rate</span>
                    <Badge variant="outline">
                      {(recovery.recoverySuccessRate * 100).toFixed(1)}%
                    </Badge>
                  </div>
                  <div className="flex items-center justify-between">
                    <span>User Intervention</span>
                    <Badge variant="outline">
                      {(recovery.userInterventionRate * 100).toFixed(1)}%
                    </Badge>
                  </div>
                </div>
              </CardContent>
            </Card>
          </div>

          {/* Top Error Patterns */}
          <Card>
            <CardHeader>
              <CardTitle>Top Error Patterns</CardTitle>
              <CardDescription>Most frequent error patterns detected</CardDescription>
            </CardHeader>
            <CardContent>
              <div className="space-y-4">
                {comprehensive.topErrorPatterns.slice(0, 5).map((pattern: any, index: number) => (
                  <div key={pattern.pattern} className="border rounded-lg p-3">
                    <div className="flex items-center justify-between mb-2">
                      <div className="flex items-center gap-2">
                        <Badge variant="outline">#{index + 1}</Badge>
                        <span className="font-medium text-sm">{pattern.pattern}</span>
                      </div>
                      <Badge>{pattern.frequency}</Badge>
                    </div>
                    <div className="text-xs text-muted-foreground">
                      <div>First: {pattern.firstOccurrence.toLocaleString()}</div>
                      <div>Last: {pattern.lastOccurrence.toLocaleString()}</div>
                    </div>
                  </div>
                ))}
              </div>
            </CardContent>
          </Card>

          {/* Current Session */}
          <Card>
            <CardHeader>
              <CardTitle>Current Session</CardTitle>
              <CardDescription>Session ID: {comprehensive.sessionMetrics.sessionId}</CardDescription>
            </CardHeader>
            <CardContent>
              <div className="grid grid-cols-2 gap-4">
                <div>
                  <div className="text-sm text-muted-foreground">Start Time</div>
                  <div className="font-medium text-sm">
                    {comprehensive.sessionMetrics.startTime.toLocaleString()}
                  </div>
                </div>
                <div>
                  <div className="text-sm text-muted-foreground">Duration</div>
                  <div className="font-medium">
                    {Math.round((Date.now() - comprehensive.sessionMetrics.startTime.getTime()) / 1000 / 60)} min
                  </div>
                </div>
                <div>
                  <div className="text-sm text-muted-foreground">Total Errors</div>
                  <div className="font-medium">{comprehensive.sessionMetrics.totalErrors}</div>
                </div>
                <div>
                  <div className="text-sm text-muted-foreground">Recovery Attempts</div>
                  <div className="font-medium">{comprehensive.sessionMetrics.recoveryAttempts}</div>
                </div>
              </div>

              {comprehensive.sessionMetrics.sessionTerminated && (
                <div className="mt-4 p-3 bg-red-50 border border-red-200 rounded-lg">
                  <div className="text-sm font-medium text-red-800">Session Terminated</div>
                  <div className="text-sm text-red-600">
                    {comprehensive.sessionMetrics.terminationReason}
                  </div>
                </div>
              )}
            </CardContent>
          </Card>
        </div>
      </div>
    </div>
  );
}

/**
 * Hook for accessing error dashboard in development
 */
export function useErrorDashboard() {
  const [showDashboard, setShowDashboard] = useState(false);

  // Only available in development
  const isAvailable = process.env.NODE_ENV === 'development';

  const toggleDashboard = () => {
    if (isAvailable) {
      setShowDashboard(!showDashboard);
    }
  };

  return {
    showDashboard,
    toggleDashboard,
    isAvailable,
    Dashboard: ErrorDashboard
  };
}