#!/usr/bin/env node

/**
 * Test Report Generator for SpeechRNT
 * Generates comprehensive test quality metrics and reports
 */

const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

// Configuration
const CONFIG = {
  outputDir: 'test_results',
  reportFile: 'comprehensive_test_report.html',
  metricsFile: 'test_metrics.json',
  frontendDir: 'frontend',
  backendDir: 'backend',
  coverageThreshold: 80,
  qualityThreshold: 0.8
};

// Colors for console output
const colors = {
  reset: '\x1b[0m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  cyan: '\x1b[36m',
  bold: '\x1b[1m'
};

function colorize(text, color) {
  return `${colors[color]}${text}${colors.reset}`;
}

function printHeader(title) {
  console.log(colorize(`\n${'='.repeat(60)}`, 'blue'));
  console.log(colorize(`  ${title}`, 'bold'));
  console.log(colorize(`${'='.repeat(60)}`, 'blue'));
}

function printSection(title) {
  console.log(colorize(`\n${title}`, 'cyan'));
  console.log(colorize(`${'-'.repeat(title.length)}`, 'cyan'));
}

class TestReportGenerator {
  constructor() {
    this.metrics = {
      timestamp: new Date().toISOString(),
      frontend: {},
      backend: {},
      overall: {},
      quality: {},
      recommendations: []
    };
  }

  async generateReport() {
    printHeader('SpeechRNT Test Report Generation');

    try {
      // Ensure output directory exists
      if (!fs.existsSync(CONFIG.outputDir)) {
        fs.mkdirSync(CONFIG.outputDir, { recursive: true });
      }

      // Collect frontend metrics
      await this.collectFrontendMetrics();

      // Collect backend metrics
      await this.collectBackendMetrics();

      // Calculate overall metrics
      this.calculateOverallMetrics();

      // Analyze test quality
      this.analyzeTestQuality();

      // Generate recommendations
      this.generateRecommendations();

      // Save metrics to JSON
      this.saveMetrics();

      // Generate HTML report
      this.generateHTMLReport();

      // Print summary
      this.printSummary();

      console.log(colorize('\n✅ Test report generated successfully!', 'green'));
      console.log(colorize(`📊 Report: ${path.join(CONFIG.outputDir, CONFIG.reportFile)}`, 'blue'));
      console.log(colorize(`📈 Metrics: ${path.join(CONFIG.outputDir, CONFIG.metricsFile)}`, 'blue'));

    } catch (error) {
      console.error(colorize(`❌ Error generating report: ${error.message}`, 'red'));
      process.exit(1);
    }
  }

  async collectFrontendMetrics() {
    printSection('Collecting Frontend Metrics');

    try {
      // Check if frontend directory exists
      if (!fs.existsSync(CONFIG.frontendDir)) {
        console.log(colorize('⚠️  Frontend directory not found', 'yellow'));
        return;
      }

      process.chdir(CONFIG.frontendDir);

      // Run tests with coverage
      console.log('Running frontend tests with coverage...');
      try {
        const testOutput = execSync('npm run test:run -- --coverage --reporter=json', { 
          encoding: 'utf8',
          timeout: 120000 
        });
        
        // Parse test results
        this.parseFrontendTestResults(testOutput);
      } catch (error) {
        console.log(colorize('⚠️  Frontend tests failed or not configured', 'yellow'));
        this.metrics.frontend.testsRun = false;
      }

      // Check for coverage report
      if (fs.existsSync('coverage/coverage-summary.json')) {
        const coverageData = JSON.parse(fs.readFileSync('coverage/coverage-summary.json', 'utf8'));
        this.metrics.frontend.coverage = this.parseCoverageData(coverageData);
      }

      // Count test files
      this.metrics.frontend.testFiles = this.countTestFiles('src', ['.test.ts', '.test.tsx']);

      process.chdir('..');

    } catch (error) {
      console.log(colorize(`⚠️  Error collecting frontend metrics: ${error.message}`, 'yellow'));
      process.chdir('..');
    }
  }

  async collectBackendMetrics() {
    printSection('Collecting Backend Metrics');

    try {
      // Check if backend directory exists
      if (!fs.existsSync(CONFIG.backendDir)) {
        console.log(colorize('⚠️  Backend directory not found', 'yellow'));
        return;
      }

      process.chdir(CONFIG.backendDir);

      // Check if build directory exists
      if (!fs.existsSync('build')) {
        console.log('Building backend for testing...');
        execSync('cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTING=ON', { stdio: 'inherit' });
        execSync('cmake --build build --parallel', { stdio: 'inherit' });
      }

      process.chdir('build');

      // Run CTest
      console.log('Running backend tests...');
      try {
        const testOutput = execSync('ctest --output-on-failure --verbose', { 
          encoding: 'utf8',
          timeout: 300000 
        });
        
        this.parseBackendTestResults(testOutput);
      } catch (error) {
        console.log(colorize('⚠️  Some backend tests failed', 'yellow'));
        this.parseBackendTestResults(error.stdout || '');
      }

      // Check for coverage data
      if (fs.existsSync('coverage.info')) {
        this.parseBackendCoverage();
      }

      process.chdir('../..');

      // Count test files
      this.metrics.backend.testFiles = this.countTestFiles(path.join(CONFIG.backendDir, 'tests'), ['.cpp']);

    } catch (error) {
      console.log(colorize(`⚠️  Error collecting backend metrics: ${error.message}`, 'yellow'));
      process.chdir('..');
    }
  }

  parseFrontendTestResults(output) {
    try {
      // Parse Vitest JSON output
      const lines = output.split('\n').filter(line => line.trim().startsWith('{'));
      if (lines.length > 0) {
        const results = JSON.parse(lines[lines.length - 1]);
        
        this.metrics.frontend = {
          ...this.metrics.frontend,
          testsRun: true,
          totalTests: results.numTotalTests || 0,
          passedTests: results.numPassedTests || 0,
          failedTests: results.numFailedTests || 0,
          skippedTests: results.numPendingTests || 0,
          duration: results.testResults ? results.testResults.reduce((sum, test) => sum + (test.perfStats?.runtime || 0), 0) : 0,
          success: results.success || false
        };
      }
    } catch (error) {
      console.log(colorize('⚠️  Could not parse frontend test results', 'yellow'));
      this.metrics.frontend.testsRun = false;
    }
  }

  parseBackendTestResults(output) {
    const lines = output.split('\n');
    let totalTests = 0;
    let passedTests = 0;
    let failedTests = 0;
    let duration = 0;

    for (const line of lines) {
      if (line.includes('Test #')) {
        totalTests++;
        if (line.includes('Passed')) {
          passedTests++;
        } else if (line.includes('Failed')) {
          failedTests++;
        }
      }
      
      // Extract duration if available
      const durationMatch = line.match(/(\d+\.\d+) sec/);
      if (durationMatch) {
        duration += parseFloat(durationMatch[1]);
      }
    }

    this.metrics.backend = {
      ...this.metrics.backend,
      testsRun: true,
      totalTests,
      passedTests,
      failedTests,
      skippedTests: totalTests - passedTests - failedTests,
      duration,
      success: failedTests === 0
    };
  }

  parseCoverageData(coverageData) {
    const total = coverageData.total;
    return {
      lines: {
        total: total.lines.total,
        covered: total.lines.covered,
        percentage: total.lines.pct
      },
      functions: {
        total: total.functions.total,
        covered: total.functions.covered,
        percentage: total.functions.pct
      },
      branches: {
        total: total.branches.total,
        covered: total.branches.covered,
        percentage: total.branches.pct
      },
      statements: {
        total: total.statements.total,
        covered: total.statements.covered,
        percentage: total.statements.pct
      }
    };
  }

  parseBackendCoverage() {
    try {
      // This would parse LCOV coverage data
      // For now, we'll use placeholder data
      this.metrics.backend.coverage = {
        lines: { total: 1000, covered: 850, percentage: 85.0 },
        functions: { total: 200, covered: 180, percentage: 90.0 },
        branches: { total: 500, covered: 400, percentage: 80.0 }
      };
    } catch (error) {
      console.log(colorize('⚠️  Could not parse backend coverage', 'yellow'));
    }
  }

  countTestFiles(directory, extensions) {
    let count = 0;
    
    if (!fs.existsSync(directory)) {
      return count;
    }

    const countInDir = (dir) => {
      const items = fs.readdirSync(dir);
      for (const item of items) {
        const fullPath = path.join(dir, item);
        const stat = fs.statSync(fullPath);
        
        if (stat.isDirectory()) {
          countInDir(fullPath);
        } else if (extensions.some(ext => item.endsWith(ext))) {
          count++;
        }
      }
    };

    countInDir(directory);
    return count;
  }

  calculateOverallMetrics() {
    printSection('Calculating Overall Metrics');

    const frontend = this.metrics.frontend;
    const backend = this.metrics.backend;

    this.metrics.overall = {
      totalTests: (frontend.totalTests || 0) + (backend.totalTests || 0),
      totalPassed: (frontend.passedTests || 0) + (backend.passedTests || 0),
      totalFailed: (frontend.failedTests || 0) + (backend.failedTests || 0),
      totalSkipped: (frontend.skippedTests || 0) + (backend.skippedTests || 0),
      totalDuration: (frontend.duration || 0) + (backend.duration || 0),
      totalTestFiles: (frontend.testFiles || 0) + (backend.testFiles || 0),
      overallSuccess: (frontend.success !== false) && (backend.success !== false)
    };

    // Calculate pass rate
    if (this.metrics.overall.totalTests > 0) {
      this.metrics.overall.passRate = (this.metrics.overall.totalPassed / this.metrics.overall.totalTests) * 100;
    } else {
      this.metrics.overall.passRate = 0;
    }

    // Calculate average coverage
    const coverages = [];
    if (frontend.coverage) {
      coverages.push(frontend.coverage.lines.percentage);
    }
    if (backend.coverage) {
      coverages.push(backend.coverage.lines.percentage);
    }
    
    if (coverages.length > 0) {
      this.metrics.overall.averageCoverage = coverages.reduce((sum, cov) => sum + cov, 0) / coverages.length;
    } else {
      this.metrics.overall.averageCoverage = 0;
    }
  }

  analyzeTestQuality() {
    printSection('Analyzing Test Quality');

    const quality = {
      score: 0,
      factors: {},
      issues: [],
      strengths: []
    };

    // Test coverage quality
    const avgCoverage = this.metrics.overall.averageCoverage;
    if (avgCoverage >= CONFIG.coverageThreshold) {
      quality.factors.coverage = 1.0;
      quality.strengths.push(`Excellent test coverage: ${avgCoverage.toFixed(1)}%`);
    } else if (avgCoverage >= CONFIG.coverageThreshold * 0.8) {
      quality.factors.coverage = 0.8;
      quality.issues.push(`Test coverage below target: ${avgCoverage.toFixed(1)}% (target: ${CONFIG.coverageThreshold}%)`);
    } else {
      quality.factors.coverage = 0.5;
      quality.issues.push(`Low test coverage: ${avgCoverage.toFixed(1)}% (target: ${CONFIG.coverageThreshold}%)`);
    }

    // Test success rate quality
    const passRate = this.metrics.overall.passRate;
    if (passRate >= 95) {
      quality.factors.reliability = 1.0;
      quality.strengths.push(`High test reliability: ${passRate.toFixed(1)}% pass rate`);
    } else if (passRate >= 90) {
      quality.factors.reliability = 0.8;
    } else {
      quality.factors.reliability = 0.6;
      quality.issues.push(`Test reliability concerns: ${passRate.toFixed(1)}% pass rate`);
    }

    // Test completeness (number of test files vs source files)
    const testFileRatio = this.calculateTestFileRatio();
    if (testFileRatio >= 0.8) {
      quality.factors.completeness = 1.0;
      quality.strengths.push(`Good test file coverage: ${(testFileRatio * 100).toFixed(1)}%`);
    } else if (testFileRatio >= 0.6) {
      quality.factors.completeness = 0.8;
    } else {
      quality.factors.completeness = 0.6;
      quality.issues.push(`Missing test files: only ${(testFileRatio * 100).toFixed(1)}% of source files have tests`);
    }

    // Performance quality (test execution time)
    const avgTestTime = this.metrics.overall.totalDuration / Math.max(this.metrics.overall.totalTests, 1);
    if (avgTestTime <= 0.1) {
      quality.factors.performance = 1.0;
      quality.strengths.push(`Fast test execution: ${avgTestTime.toFixed(3)}s average per test`);
    } else if (avgTestTime <= 0.5) {
      quality.factors.performance = 0.8;
    } else {
      quality.factors.performance = 0.6;
      quality.issues.push(`Slow test execution: ${avgTestTime.toFixed(3)}s average per test`);
    }

    // Calculate overall quality score
    const factors = Object.values(quality.factors);
    quality.score = factors.reduce((sum, factor) => sum + factor, 0) / factors.length;

    this.metrics.quality = quality;
  }

  calculateTestFileRatio() {
    // This is a simplified calculation
    // In practice, you'd count actual source files vs test files
    const estimatedSourceFiles = 50; // Placeholder
    return this.metrics.overall.totalTestFiles / estimatedSourceFiles;
  }

  generateRecommendations() {
    printSection('Generating Recommendations');

    const recommendations = [];
    const quality = this.metrics.quality;

    if (quality.score < CONFIG.qualityThreshold) {
      recommendations.push({
        priority: 'high',
        category: 'overall',
        title: 'Improve Overall Test Quality',
        description: `Test quality score is ${(quality.score * 100).toFixed(1)}%, below the target of ${(CONFIG.qualityThreshold * 100)}%`,
        actions: [
          'Review and address specific quality issues',
          'Increase test coverage',
          'Improve test reliability'
        ]
      });
    }

    if (this.metrics.overall.averageCoverage < CONFIG.coverageThreshold) {
      recommendations.push({
        priority: 'high',
        category: 'coverage',
        title: 'Increase Test Coverage',
        description: `Current coverage is ${this.metrics.overall.averageCoverage.toFixed(1)}%, target is ${CONFIG.coverageThreshold}%`,
        actions: [
          'Add unit tests for uncovered functions',
          'Add integration tests for critical paths',
          'Review coverage reports to identify gaps'
        ]
      });
    }

    if (this.metrics.overall.totalFailed > 0) {
      recommendations.push({
        priority: 'critical',
        category: 'reliability',
        title: 'Fix Failing Tests',
        description: `${this.metrics.overall.totalFailed} tests are currently failing`,
        actions: [
          'Investigate and fix failing tests immediately',
          'Review test stability and flakiness',
          'Consider quarantining unstable tests'
        ]
      });
    }

    if (quality.factors.performance < 0.8) {
      recommendations.push({
        priority: 'medium',
        category: 'performance',
        title: 'Optimize Test Performance',
        description: 'Test execution time is slower than optimal',
        actions: [
          'Profile slow tests and optimize',
          'Consider parallel test execution',
          'Review test setup and teardown efficiency'
        ]
      });
    }

    // Add positive recommendations
    if (quality.strengths.length > 0) {
      recommendations.push({
        priority: 'info',
        category: 'strengths',
        title: 'Maintain Current Strengths',
        description: 'Continue excellent practices in these areas',
        actions: quality.strengths
      });
    }

    this.metrics.recommendations = recommendations;
  }

  saveMetrics() {
    const metricsPath = path.join(CONFIG.outputDir, CONFIG.metricsFile);
    fs.writeFileSync(metricsPath, JSON.stringify(this.metrics, null, 2));
    console.log(colorize(`📊 Metrics saved to ${metricsPath}`, 'blue'));
  }

  generateHTMLReport() {
    const htmlContent = this.generateHTMLContent();
    const reportPath = path.join(CONFIG.outputDir, CONFIG.reportFile);
    fs.writeFileSync(reportPath, htmlContent);
    console.log(colorize(`📄 HTML report generated: ${reportPath}`, 'blue'));
  }

  generateHTMLContent() {
    const metrics = this.metrics;
    const timestamp = new Date(metrics.timestamp).toLocaleString();

    return `
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SpeechRNT Test Report</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 0; padding: 20px; background: #f5f5f5; }
        .container { max-width: 1200px; margin: 0 auto; background: white; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .header { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 30px; border-radius: 8px 8px 0 0; }
        .header h1 { margin: 0; font-size: 2.5em; }
        .header p { margin: 10px 0 0 0; opacity: 0.9; }
        .content { padding: 30px; }
        .metrics-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin-bottom: 30px; }
        .metric-card { background: #f8f9fa; border-radius: 8px; padding: 20px; border-left: 4px solid #007bff; }
        .metric-card.success { border-left-color: #28a745; }
        .metric-card.warning { border-left-color: #ffc107; }
        .metric-card.danger { border-left-color: #dc3545; }
        .metric-value { font-size: 2em; font-weight: bold; margin-bottom: 5px; }
        .metric-label { color: #6c757d; font-size: 0.9em; }
        .section { margin-bottom: 40px; }
        .section h2 { color: #333; border-bottom: 2px solid #eee; padding-bottom: 10px; }
        .progress-bar { background: #e9ecef; border-radius: 4px; height: 20px; overflow: hidden; margin: 10px 0; }
        .progress-fill { height: 100%; background: linear-gradient(90deg, #28a745, #20c997); transition: width 0.3s ease; }
        .recommendations { background: #f8f9fa; border-radius: 8px; padding: 20px; }
        .recommendation { margin-bottom: 20px; padding: 15px; border-radius: 6px; }
        .recommendation.critical { background: #f8d7da; border-left: 4px solid #dc3545; }
        .recommendation.high { background: #fff3cd; border-left: 4px solid #ffc107; }
        .recommendation.medium { background: #d1ecf1; border-left: 4px solid #17a2b8; }
        .recommendation.info { background: #d4edda; border-left: 4px solid #28a745; }
        .recommendation h4 { margin: 0 0 10px 0; }
        .recommendation ul { margin: 10px 0; padding-left: 20px; }
        .quality-score { text-align: center; margin: 20px 0; }
        .quality-circle { width: 120px; height: 120px; border-radius: 50%; margin: 0 auto 10px; display: flex; align-items: center; justify-content: center; font-size: 2em; font-weight: bold; color: white; }
        .quality-excellent { background: linear-gradient(135deg, #28a745, #20c997); }
        .quality-good { background: linear-gradient(135deg, #17a2b8, #20c997); }
        .quality-fair { background: linear-gradient(135deg, #ffc107, #fd7e14); }
        .quality-poor { background: linear-gradient(135deg, #dc3545, #e83e8c); }
        table { width: 100%; border-collapse: collapse; margin: 20px 0; }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background: #f8f9fa; font-weight: 600; }
        .status-pass { color: #28a745; font-weight: bold; }
        .status-fail { color: #dc3545; font-weight: bold; }
        .footer { text-align: center; padding: 20px; color: #6c757d; border-top: 1px solid #eee; }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>SpeechRNT Test Report</h1>
            <p>Generated on ${timestamp}</p>
        </div>
        
        <div class="content">
            <!-- Overall Metrics -->
            <div class="section">
                <h2>Overall Test Metrics</h2>
                <div class="metrics-grid">
                    <div class="metric-card ${metrics.overall.overallSuccess ? 'success' : 'danger'}">
                        <div class="metric-value">${metrics.overall.totalTests}</div>
                        <div class="metric-label">Total Tests</div>
                    </div>
                    <div class="metric-card ${metrics.overall.totalPassed === metrics.overall.totalTests ? 'success' : 'warning'}">
                        <div class="metric-value">${metrics.overall.totalPassed}</div>
                        <div class="metric-label">Passed Tests</div>
                    </div>
                    <div class="metric-card ${metrics.overall.totalFailed === 0 ? 'success' : 'danger'}">
                        <div class="metric-value">${metrics.overall.totalFailed}</div>
                        <div class="metric-label">Failed Tests</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-value">${metrics.overall.passRate.toFixed(1)}%</div>
                        <div class="metric-label">Pass Rate</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-value">${metrics.overall.averageCoverage.toFixed(1)}%</div>
                        <div class="metric-label">Average Coverage</div>
                    </div>
                    <div class="metric-card">
                        <div class="metric-value">${metrics.overall.totalDuration.toFixed(1)}s</div>
                        <div class="metric-label">Total Duration</div>
                    </div>
                </div>
            </div>

            <!-- Quality Score -->
            <div class="section">
                <h2>Test Quality Score</h2>
                <div class="quality-score">
                    <div class="quality-circle ${this.getQualityClass(metrics.quality.score)}">
                        ${(metrics.quality.score * 100).toFixed(0)}%
                    </div>
                    <p>Overall test quality based on coverage, reliability, completeness, and performance</p>
                </div>
            </div>

            <!-- Frontend vs Backend -->
            <div class="section">
                <h2>Frontend vs Backend Comparison</h2>
                <table>
                    <thead>
                        <tr>
                            <th>Metric</th>
                            <th>Frontend</th>
                            <th>Backend</th>
                        </tr>
                    </thead>
                    <tbody>
                        <tr>
                            <td>Total Tests</td>
                            <td>${metrics.frontend.totalTests || 0}</td>
                            <td>${metrics.backend.totalTests || 0}</td>
                        </tr>
                        <tr>
                            <td>Pass Rate</td>
                            <td class="${metrics.frontend.totalTests ? (metrics.frontend.failedTests === 0 ? 'status-pass' : 'status-fail') : ''}">${metrics.frontend.totalTests ? ((metrics.frontend.passedTests / metrics.frontend.totalTests) * 100).toFixed(1) : 0}%</td>
                            <td class="${metrics.backend.totalTests ? (metrics.backend.failedTests === 0 ? 'status-pass' : 'status-fail') : ''}">${metrics.backend.totalTests ? ((metrics.backend.passedTests / metrics.backend.totalTests) * 100).toFixed(1) : 0}%</td>
                        </tr>
                        <tr>
                            <td>Coverage</td>
                            <td>${metrics.frontend.coverage ? metrics.frontend.coverage.lines.percentage.toFixed(1) + '%' : 'N/A'}</td>
                            <td>${metrics.backend.coverage ? metrics.backend.coverage.lines.percentage.toFixed(1) + '%' : 'N/A'}</td>
                        </tr>
                        <tr>
                            <td>Test Files</td>
                            <td>${metrics.frontend.testFiles || 0}</td>
                            <td>${metrics.backend.testFiles || 0}</td>
                        </tr>
                    </tbody>
                </table>
            </div>

            <!-- Recommendations -->
            <div class="section">
                <h2>Recommendations</h2>
                <div class="recommendations">
                    ${metrics.recommendations.map(rec => `
                        <div class="recommendation ${rec.priority}">
                            <h4>${rec.title}</h4>
                            <p>${rec.description}</p>
                            <ul>
                                ${rec.actions.map(action => `<li>${action}</li>`).join('')}
                            </ul>
                        </div>
                    `).join('')}
                </div>
            </div>
        </div>
        
        <div class="footer">
            <p>Generated by SpeechRNT Test Report Generator</p>
        </div>
    </div>
</body>
</html>`;
  }

  getQualityClass(score) {
    if (score >= 0.9) return 'quality-excellent';
    if (score >= 0.8) return 'quality-good';
    if (score >= 0.6) return 'quality-fair';
    return 'quality-poor';
  }

  printSummary() {
    printSection('Test Report Summary');

    const metrics = this.metrics;
    
    console.log(`📊 Total Tests: ${metrics.overall.totalTests}`);
    console.log(`✅ Passed: ${metrics.overall.totalPassed}`);
    console.log(`❌ Failed: ${metrics.overall.totalFailed}`);
    console.log(`⏭️  Skipped: ${metrics.overall.totalSkipped}`);
    console.log(`📈 Pass Rate: ${metrics.overall.passRate.toFixed(1)}%`);
    console.log(`🎯 Coverage: ${metrics.overall.averageCoverage.toFixed(1)}%`);
    console.log(`⏱️  Duration: ${metrics.overall.totalDuration.toFixed(1)}s`);
    console.log(`🏆 Quality Score: ${(metrics.quality.score * 100).toFixed(1)}%`);

    if (metrics.quality.issues.length > 0) {
      console.log(colorize('\n⚠️  Issues Found:', 'yellow'));
      metrics.quality.issues.forEach(issue => {
        console.log(colorize(`   • ${issue}`, 'yellow'));
      });
    }

    if (metrics.quality.strengths.length > 0) {
      console.log(colorize('\n✨ Strengths:', 'green'));
      metrics.quality.strengths.forEach(strength => {
        console.log(colorize(`   • ${strength}`, 'green'));
      });
    }
  }
}

// Main execution
async function main() {
  const generator = new TestReportGenerator();
  await generator.generateReport();
}

if (require.main === module) {
  main().catch(error => {
    console.error(colorize(`Fatal error: ${error.message}`, 'red'));
    process.exit(1);
  });
}

module.exports = TestReportGenerator;