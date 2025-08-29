#!/usr/bin/env node

/**
 * Test validation script for SpeechRNT project
 * Validates that all test files are properly structured and can be discovered
 */

const fs = require('fs');
const path = require('path');

// Configuration
const BACKEND_TEST_DIR = 'backend/tests';
const FRONTEND_TEST_DIR = 'frontend/src';
const REQUIRED_BACKEND_TESTS = [
    'unit/test_websocket_message_protocol.cpp',
    'unit/test_audio_buffer.cpp',
    'integration/test_end_to_end_conversation.cpp',
    'performance/load_testing.cpp',
    'fixtures/test_data_generator.cpp'
];

const REQUIRED_FRONTEND_TESTS = [
    'test/conversationFlowIntegration.test.tsx',
    'test/performanceTests.test.tsx'
];

// Colors for console output
const colors = {
    reset: '\x1b[0m',
    red: '\x1b[31m',
    green: '\x1b[32m',
    yellow: '\x1b[33m',
    blue: '\x1b[34m',
    cyan: '\x1b[36m'
};

function colorize(text, color) {
    return `${colors[color]}${text}${colors.reset}`;
}

function printHeader(title) {
    console.log(colorize(`\n${title}`, 'blue'));
    console.log(colorize('='.repeat(title.length), 'blue'));
}

function printSection(title) {
    console.log(colorize(`\n${title}`, 'yellow'));
    console.log(colorize('-'.repeat(title.length), 'yellow'));
}

function checkFileExists(filePath) {
    try {
        return fs.existsSync(filePath);
    } catch (error) {
        return false;
    }
}

function validateCppTestFile(filePath) {
    try {
        const content = fs.readFileSync(filePath, 'utf8');
        const issues = [];
        
        // Check for required includes
        if (!content.includes('#include <gtest/gtest.h>') && !content.includes('TEST_F(') && !content.includes('TEST(')) {
            // Check if it's a simple test without gtest
            if (!content.includes('int main(') && !content.includes('void test')) {
                issues.push('No test framework detected (neither GTest nor simple tests)');
            }
        }
        
        // Check for test cases
        const testCaseRegex = /TEST_F?\s*\(\s*\w+\s*,\s*\w+\s*\)/g;
        const testCases = content.match(testCaseRegex);
        if (!testCases && !content.includes('int main(')) {
            issues.push('No test cases found');
        }
        
        // Check for proper namespace usage
        if (content.includes('using namespace std;') && !content.includes('// NOLINT')) {
            issues.push('Avoid "using namespace std;" in test files');
        }
        
        // Check for memory leaks potential
        if (content.includes('new ') && !content.includes('delete ') && !content.includes('unique_ptr') && !content.includes('shared_ptr')) {
            issues.push('Potential memory leak: "new" without corresponding cleanup');
        }
        
        return {
            valid: issues.length === 0,
            issues: issues,
            testCount: testCases ? testCases.length : (content.includes('int main(') ? 1 : 0)
        };
    } catch (error) {
        return {
            valid: false,
            issues: [`Failed to read file: ${error.message}`],
            testCount: 0
        };
    }
}

function validateTsxTestFile(filePath) {
    try {
        const content = fs.readFileSync(filePath, 'utf8');
        const issues = [];
        
        // Check for required imports
        if (!content.includes('import') || !content.includes('vitest')) {
            issues.push('Missing vitest imports');
        }
        
        // Check for test cases
        const testCaseRegex = /(?:it|test)\s*\(\s*['"`][^'"`]*['"`]\s*,/g;
        const testCases = content.match(testCaseRegex);
        if (!testCases) {
            issues.push('No test cases found');
        }
        
        // Check for describe blocks
        const describeRegex = /describe\s*\(\s*['"`][^'"`]*['"`]\s*,/g;
        const describeBlocks = content.match(describeRegex);
        if (!describeBlocks) {
            issues.push('No describe blocks found (recommended for organization)');
        }
        
        // Check for proper cleanup
        if (content.includes('beforeEach') && !content.includes('afterEach')) {
            issues.push('beforeEach found but no afterEach (potential cleanup issue)');
        }
        
        // Check for async/await usage
        if (content.includes('async') && !content.includes('await')) {
            issues.push('async function without await usage');
        }
        
        // Check for proper mocking
        if (content.includes('mock') && !content.includes('vi.clearAllMocks')) {
            issues.push('Mocks used but no cleanup in afterEach');
        }
        
        return {
            valid: issues.length === 0,
            issues: issues,
            testCount: testCases ? testCases.length : 0,
            describeCount: describeBlocks ? describeBlocks.length : 0
        };
    } catch (error) {
        return {
            valid: false,
            issues: [`Failed to read file: ${error.message}`],
            testCount: 0,
            describeCount: 0
        };
    }
}

function validateBackendTests() {
    printSection('Backend Test Validation');
    
    let totalTests = 0;
    let validFiles = 0;
    let totalIssues = 0;
    
    console.log('Checking required backend test files...');
    
    for (const testFile of REQUIRED_BACKEND_TESTS) {
        const fullPath = path.join(BACKEND_TEST_DIR, testFile);
        const exists = checkFileExists(fullPath);
        
        if (exists) {
            console.log(colorize(`✓ ${testFile}`, 'green'));
            
            const validation = validateCppTestFile(fullPath);
            totalTests += validation.testCount;
            
            if (validation.valid) {
                validFiles++;
                console.log(`  ${validation.testCount} test cases found`);
            } else {
                totalIssues += validation.issues.length;
                console.log(colorize(`  Issues found:`, 'red'));
                validation.issues.forEach(issue => {
                    console.log(colorize(`    - ${issue}`, 'red'));
                });
            }
        } else {
            console.log(colorize(`✗ ${testFile} (missing)`, 'red'));
            totalIssues++;
        }
    }
    
    // Scan for additional test files
    console.log('\nScanning for additional test files...');
    const additionalTests = [];
    
    function scanDirectory(dir, relativePath = '') {
        try {
            const items = fs.readdirSync(dir);
            items.forEach(item => {
                const fullPath = path.join(dir, item);
                const relPath = path.join(relativePath, item);
                
                if (fs.statSync(fullPath).isDirectory()) {
                    scanDirectory(fullPath, relPath);
                } else if (item.endsWith('.cpp') && item.includes('test')) {
                    const requiredPath = relPath.replace(/\\/g, '/');
                    if (!REQUIRED_BACKEND_TESTS.includes(requiredPath)) {
                        additionalTests.push(requiredPath);
                    }
                }
            });
        } catch (error) {
            // Directory might not exist, skip
        }
    }
    
    scanDirectory(BACKEND_TEST_DIR);
    
    additionalTests.forEach(testFile => {
        const fullPath = path.join(BACKEND_TEST_DIR, testFile);
        console.log(colorize(`+ ${testFile}`, 'cyan'));
        
        const validation = validateCppTestFile(fullPath);
        totalTests += validation.testCount;
        
        if (validation.valid) {
            validFiles++;
            console.log(`  ${validation.testCount} test cases found`);
        } else {
            totalIssues += validation.issues.length;
            console.log(colorize(`  Issues found:`, 'yellow'));
            validation.issues.forEach(issue => {
                console.log(colorize(`    - ${issue}`, 'yellow'));
            });
        }
    });
    
    console.log(`\nBackend Summary:`);
    console.log(`  Total test files: ${REQUIRED_BACKEND_TESTS.length + additionalTests.length}`);
    console.log(`  Valid files: ${validFiles}`);
    console.log(`  Total test cases: ${totalTests}`);
    console.log(`  Issues found: ${totalIssues}`);
    
    return {
        totalFiles: REQUIRED_BACKEND_TESTS.length + additionalTests.length,
        validFiles,
        totalTests,
        totalIssues
    };
}

function validateFrontendTests() {
    printSection('Frontend Test Validation');
    
    let totalTests = 0;
    let totalDescribes = 0;
    let validFiles = 0;
    let totalIssues = 0;
    
    console.log('Checking required frontend test files...');
    
    for (const testFile of REQUIRED_FRONTEND_TESTS) {
        const fullPath = path.join(FRONTEND_TEST_DIR, testFile);
        const exists = checkFileExists(fullPath);
        
        if (exists) {
            console.log(colorize(`✓ ${testFile}`, 'green'));
            
            const validation = validateTsxTestFile(fullPath);
            totalTests += validation.testCount;
            totalDescribes += validation.describeCount;
            
            if (validation.valid) {
                validFiles++;
                console.log(`  ${validation.testCount} test cases in ${validation.describeCount} describe blocks`);
            } else {
                totalIssues += validation.issues.length;
                console.log(colorize(`  Issues found:`, 'red'));
                validation.issues.forEach(issue => {
                    console.log(colorize(`    - ${issue}`, 'red'));
                });
            }
        } else {
            console.log(colorize(`✗ ${testFile} (missing)`, 'red'));
            totalIssues++;
        }
    }
    
    // Scan for additional test files
    console.log('\nScanning for additional test files...');
    const additionalTests = [];
    
    function scanDirectory(dir, relativePath = '') {
        try {
            const items = fs.readdirSync(dir);
            items.forEach(item => {
                const fullPath = path.join(dir, item);
                const relPath = path.join(relativePath, item);
                
                if (fs.statSync(fullPath).isDirectory()) {
                    scanDirectory(fullPath, relPath);
                } else if (item.endsWith('.test.tsx') || item.endsWith('.test.ts')) {
                    const requiredPath = relPath.replace(/\\/g, '/');
                    if (!REQUIRED_FRONTEND_TESTS.includes(requiredPath)) {
                        additionalTests.push(requiredPath);
                    }
                }
            });
        } catch (error) {
            // Directory might not exist, skip
        }
    }
    
    scanDirectory(FRONTEND_TEST_DIR);
    
    additionalTests.forEach(testFile => {
        const fullPath = path.join(FRONTEND_TEST_DIR, testFile);
        console.log(colorize(`+ ${testFile}`, 'cyan'));
        
        const validation = validateTsxTestFile(fullPath);
        totalTests += validation.testCount;
        totalDescribes += validation.describeCount;
        
        if (validation.valid) {
            validFiles++;
            console.log(`  ${validation.testCount} test cases in ${validation.describeCount} describe blocks`);
        } else {
            totalIssues += validation.issues.length;
            console.log(colorize(`  Issues found:`, 'yellow'));
            validation.issues.forEach(issue => {
                console.log(colorize(`    - ${issue}`, 'yellow'));
            });
        }
    });
    
    console.log(`\nFrontend Summary:`);
    console.log(`  Total test files: ${REQUIRED_FRONTEND_TESTS.length + additionalTests.length}`);
    console.log(`  Valid files: ${validFiles}`);
    console.log(`  Total test cases: ${totalTests}`);
    console.log(`  Total describe blocks: ${totalDescribes}`);
    console.log(`  Issues found: ${totalIssues}`);
    
    return {
        totalFiles: REQUIRED_FRONTEND_TESTS.length + additionalTests.length,
        validFiles,
        totalTests,
        totalDescribes,
        totalIssues
    };
}

function validateTestConfiguration() {
    printSection('Test Configuration Validation');
    
    const configFiles = [
        { path: 'backend/tests/CMakeLists.txt', type: 'CMake' },
        { path: 'frontend/vitest.config.ts', type: 'Vitest' },
        { path: 'frontend/package.json', type: 'Package.json' }
    ];
    
    let validConfigs = 0;
    
    configFiles.forEach(config => {
        if (checkFileExists(config.path)) {
            console.log(colorize(`✓ ${config.path} (${config.type})`, 'green'));
            validConfigs++;
            
            // Additional validation for specific config types
            if (config.type === 'Package.json') {
                try {
                    const packageJson = JSON.parse(fs.readFileSync(config.path, 'utf8'));
                    const hasTestScript = packageJson.scripts && packageJson.scripts.test;
                    const hasVitest = packageJson.devDependencies && packageJson.devDependencies.vitest;
                    
                    if (!hasTestScript) {
                        console.log(colorize(`  Warning: No test script found`, 'yellow'));
                    }
                    if (!hasVitest) {
                        console.log(colorize(`  Warning: Vitest not found in devDependencies`, 'yellow'));
                    }
                } catch (error) {
                    console.log(colorize(`  Error parsing package.json: ${error.message}`, 'red'));
                }
            }
        } else {
            console.log(colorize(`✗ ${config.path} (missing)`, 'red'));
        }
    });
    
    console.log(`\nConfiguration Summary:`);
    console.log(`  Valid configurations: ${validConfigs}/${configFiles.length}`);
    
    return validConfigs === configFiles.length;
}

function main() {
    printHeader('SpeechRNT Test Validation');
    
    const backendResults = validateBackendTests();
    const frontendResults = validateFrontendTests();
    const configValid = validateTestConfiguration();
    
    printSection('Overall Summary');
    
    const totalFiles = backendResults.totalFiles + frontendResults.totalFiles;
    const totalValidFiles = backendResults.validFiles + frontendResults.validFiles;
    const totalTestCases = backendResults.totalTests + frontendResults.totalTests;
    const totalIssues = backendResults.totalIssues + frontendResults.totalIssues;
    
    console.log(`Total test files: ${totalFiles}`);
    console.log(`Valid test files: ${totalValidFiles}`);
    console.log(`Total test cases: ${totalTestCases}`);
    console.log(`Frontend describe blocks: ${frontendResults.totalDescribes}`);
    console.log(`Configuration valid: ${configValid ? 'Yes' : 'No'}`);
    console.log(`Total issues: ${totalIssues}`);
    
    if (totalIssues === 0 && configValid && totalValidFiles === totalFiles) {
        console.log(colorize('\n✓ All tests are properly structured and ready to run!', 'green'));
        process.exit(0);
    } else {
        console.log(colorize('\n✗ Some issues found. Please review and fix before running tests.', 'red'));
        process.exit(1);
    }
}

// Run validation
main();