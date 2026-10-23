/**
 * ConceptMap JSON 驗證腳本
 * 用途：在瀏覽器控制台中執行，驗證 Map 檔的 JSON 格式
 * 使用方法：打開 Map 檔 → F12 開啟控制台 → 複製貼上此腳本 → Enter 執行
 */

// ============================================
// 第一層：正則表達式預檢
// ============================================
function preCheckJSON() {
    console.log('🔍 開始執行第一層檢查：正則表達式預檢...\n');

    const mapDataEl = document.getElementById('map-data');
    if (!mapDataEl) {
        console.error('❌ 找不到 map-data 元素');
        return false;
    }

    const jsonText = mapDataEl.textContent;
    let hasError = false;
    let errorCount = 0;

    // 檢查 1: 數字後多雙引號 (如: 440")
    console.log('檢查 1: 數字後是否有多餘雙引號...');
    const pattern1 = /:\s*\d+"/g;
    const matches1 = jsonText.match(pattern1);
    if (matches1) {
        errorCount += matches1.length;
        console.error('❌ 發現 ' + matches1.length + ' 處數字後有多餘雙引號:');
        matches1.forEach(m => console.error('   ' + m));
        console.error('   修正方法: 移除雙引號，例如 "y": 440" 改為 "y": 440,');
        hasError = true;
    } else {
        console.log('✅ 通過');
    }

    // 檢查 2: 數字被引號包住 (如: "100")
    console.log('\n檢查 2: 數字是否被引號包住...');
    const pattern2 = /(?<!"id":\s*):\s*"\d+"/g;
    const matches2 = jsonText.match(pattern2);
    if (matches2) {
        errorCount += matches2.length;
        console.error('❌ 發現 ' + matches2.length + ' 處數字被引號包住:');
        matches2.forEach(m => console.error('   ' + m));
        console.error('   修正方法: 移除引號，例如 "x": "100" 改為 "x": 100');
        hasError = true;
    } else {
        console.log('✅ 通過');
    }

    // 檢查 3: 布林值被引號包住
    console.log('\n檢查 3: 布林值是否被引號包住...');
    const pattern3 = /"(true|false)"/g;
    const matches3 = jsonText.match(pattern3);
    if (matches3) {
        errorCount += matches3.length;
        console.error('❌ 發現 ' + matches3.length + ' 處布林值被引號包住:');
        matches3.forEach(m => console.error('   ' + m));
        console.error('   修正方法: 移除引號，例如 "completed": "false" 改為 "completed": false');
        hasError = true;
    } else {
        console.log('✅ 通過');
    }

    console.log('\n' + '='.repeat(50));
    if (!hasError) {
        console.log('✅ 第一層檢查通過：未發現常見格式錯誤');
        return true;
    } else {
        console.error('❌ 第一層檢查失敗：發現 ' + errorCount + ' 處格式錯誤');
        console.error('請修正上述錯誤後再繼續');
        return false;
    }
}

// ============================================
// 第二層：數據類型驗證
// ============================================
function validateMapJSON() {
    console.log('\n🔍 開始執行第二層檢查：數據類型驗證...\n');

    try {
        const mapDataEl = document.getElementById('map-data');
        if (!mapDataEl) {
            console.error('❌ 找不到 map-data 元素');
            return false;
        }

        const data = JSON.parse(mapDataEl.textContent);
        let errors = [];

        // 檢查節點
        console.log('檢查節點數據類型...');
        data.data.nodes?.forEach((node, i) => {
            if (typeof node.x !== 'number') errors.push(`節點 ${i} (${node.id}): x 應為數字，實際為 ${typeof node.x}`);
            if (typeof node.y !== 'number') errors.push(`節點 ${i} (${node.id}): y 應為數字，實際為 ${typeof node.y}`);
            if (typeof node.width !== 'number') errors.push(`節點 ${i} (${node.id}): width 應為數字，實際為 ${typeof node.width}`);
            if (typeof node.height !== 'number') errors.push(`節點 ${i} (${node.id}): height 應為數字，實際為 ${typeof node.height}`);
            if (node.completed !== undefined && typeof node.completed !== 'boolean') {
                errors.push(`節點 ${i} (${node.id}): completed 應為布林值，實際為 ${typeof node.completed}`);
            }
            if (node.abandoned !== undefined && typeof node.abandoned !== 'boolean') {
                errors.push(`節點 ${i} (${node.id}): abandoned 應為布林值，實際為 ${typeof node.abandoned}`);
            }
        });

        // 檢查框架
        console.log('檢查框架數據類型...');
        data.data.frames?.forEach((frame, i) => {
            if (typeof frame.x !== 'number') errors.push(`框架 ${i} (${frame.id}): x 應為數字，實際為 ${typeof frame.x}`);
            if (typeof frame.y !== 'number') errors.push(`框架 ${i} (${frame.id}): y 應為數字，實際為 ${typeof frame.y}`);
            if (typeof frame.width !== 'number') errors.push(`框架 ${i} (${frame.id}): width 應為數字，實際為 ${typeof frame.width}`);
            if (typeof frame.height !== 'number') errors.push(`框架 ${i} (${frame.id}): height 應為數字，實際為 ${typeof frame.height}`);
            if (frame.zIndex !== undefined && typeof frame.zIndex !== 'number') {
                errors.push(`框架 ${i} (${frame.id}): zIndex 應為數字，實際為 ${typeof frame.zIndex}`);
            }
            if (frame.completed !== undefined && typeof frame.completed !== 'boolean') {
                errors.push(`框架 ${i} (${frame.id}): completed 應為布林值，實際為 ${typeof frame.completed}`);
            }
        });

        console.log('\n' + '='.repeat(50));
        if (errors.length > 0) {
            console.error('❌ 第二層檢查失敗：發現 ' + errors.length + ' 處數據類型錯誤');
            errors.forEach(err => console.error('  - ' + err));
            return false;
        }

        console.log('✅ 第二層檢查通過：所有數據類型正確');
        return true;

    } catch (e) {
        console.error('❌ JSON 解析失敗:', e.message);
        console.error('這通常表示 JSON 語法有錯誤（如缺少逗號、多餘逗號、括號不匹配等）');
        return false;
    }
}

// ============================================
// 完整驗證流程
// ============================================
function fullValidation() {
    console.clear();
    console.log('═'.repeat(60));
    console.log('   ConceptMap JSON 完整驗證');
    console.log('═'.repeat(60) + '\n');

    const step1 = preCheckJSON();

    if (!step1) {
        console.log('\n⚠️  第一層檢查失敗，已中止驗證流程');
        console.log('請先修正格式錯誤後再重新驗證');
        return false;
    }

    const step2 = validateMapJSON();

    console.log('\n' + '═'.repeat(60));
    if (step1 && step2) {
        console.log('🎉 驗證完成：所有檢查都通過！');
        console.log('您的 Map 檔 JSON 格式正確');
    } else {
        console.log('❌ 驗證失敗：請修正上述錯誤');
    }
    console.log('═'.repeat(60));

    return step1 && step2;
}

// ============================================
// 執行驗證
// ============================================
console.log('ConceptMap JSON 驗證腳本已載入');
console.log('執行 fullValidation() 開始完整驗證');
console.log('或分別執行：');
console.log('  - preCheckJSON()      (第一層：正則表達式預檢)');
console.log('  - validateMapJSON()   (第二層：數據類型驗證)');
console.log('');

// 自動執行完整驗證
fullValidation();
