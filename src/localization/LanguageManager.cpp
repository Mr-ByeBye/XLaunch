#include "localization/LanguageManager.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

namespace xlaunch
{
    namespace
    {
        std::filesystem::path g_languageDirectory;
        std::unordered_map<std::string, std::string> g_strings;
        std::vector<std::string> g_languages;
        std::string g_currentLanguage = "zh-cn";

        constexpr const char* kSimplified = R"INI([strings]
语言=语言
常用=常用
%s；按住 %s 可连续启动并临时置顶=%s；按住 %s 可连续启动并临时置顶
临时钉住（松开按键后取消置顶）=临时钉住（松开按键后取消置顶）
取消钉住=取消钉住
钉住=钉住
窗口外观=窗口外观
外观=外观
分类栏布局=分类栏布局
呼出与位置=呼出与位置
系统与备份=系统与备份
标题=标题
居中=居中
显示名称=显示名称
显示边框=显示边框
图标布局=图标布局
图标=图标
横距=横距
纵距=纵距
紧凑模式列宽度=紧凑模式列宽度
项目启动方式=项目启动方式
单击启动=单击启动
双击启动=双击启动
整体透明度=整体透明度
分类栏位置=分类栏位置
布局=布局
顶部单行=顶部单行
顶部自动换行=顶部自动换行
左侧=左侧
右侧=右侧
侧边栏最大宽度=侧边栏最大宽度
横向文字=横向文字
竖向文字=竖向文字
文字方向=文字方向
从上到下=从上到下
从下到上=从下到上
阅读方向=阅读方向
分类切换=分类切换
鼠标点击=鼠标点击
鼠标悬停=鼠标悬停
切换方式=切换方式
悬停延迟（毫秒）=悬停延迟（毫秒）
快捷呼出=快捷呼出
启用快捷呼出=启用快捷呼出
启用键盘快捷键=启用键盘快捷键
启用鼠标快捷键=启用鼠标快捷键
鼠标按键=鼠标按键
键盘快捷键=键盘快捷键
快捷键=快捷键
请按下快捷键…=请按下快捷键…
取消录制=取消录制
录制=录制
无=无
中键=中键
侧键 1=侧键 1
侧键 2=侧键 2
触发按键=触发按键
组合键=组合键
双击=双击
连续启动=连续启动
临时钉住按键=临时钉住按键
主界面启动位置=主界面启动位置
屏幕正中=屏幕正中
屏幕角落=屏幕角落
用户自定义=用户自定义
鼠标位置=鼠标位置
位置=位置
左上=左上
右上=右上
左下=左下
右下=右下
使用当前位置=使用当前位置
开机自启=开机自启
自启优先级=自启优先级
默认=默认
高=高
极高(不推荐)=极高(不推荐)
"高"使用高于正常优先级,"极高"使用高优先级.="高"使用高于正常优先级,"极高"使用高优先级.
自动备份=自动备份
份=份
立即备份=立即备份
导出=导出
导入=导入
配置目录=配置目录
关闭=关闭
软件设置=软件设置
显示 XLaunch=显示 XLaunch
隐藏 XLaunch=隐藏 XLaunch
作者主页=作者主页
退出=退出
打开设置=打开设置
启动=启动
启动并保持显示=启动并保持显示
以管理员身份运行=以管理员身份运行
打开所在位置=打开所在位置
编辑=编辑
复制=复制
移动到分类=移动到分类
删除=删除
显示方式=显示方式
图标网格=图标网格
紧凑列表=紧凑列表
新增启动项目=新增启动项目
添加系统图标=添加系统图标
新增分类=新增分类
新增=新增
重命名=重命名
取消=取消
保存=保存
确定=确定
启动项编辑=启动项编辑
编辑启动项目=编辑启动项目
自定义名称=自定义名称
目标路径=目标路径
浏览文件=浏览文件
浏览文件夹=浏览文件夹
启动参数=启动参数
工作目录=工作目录
自定义图标路径=自定义图标路径
默认以管理员身份运行=默认以管理员身份运行
全局快捷键=全局快捷键
软件内快捷键=软件内快捷键
清除=清除
顶部单行支持横向滚动；顶部换行会根据窗口宽度自动增加行数。=顶部单行支持横向滚动；顶部换行会根据窗口宽度自动增加行数。
按住时可连续启动项目并保持主界面置顶=按住时可连续启动项目并保持主界面置顶
每次呼出时定位到鼠标附近=每次呼出时定位到鼠标附近
显示在主屏幕工作区域正中=显示在主屏幕工作区域正中
分类名称=分类名称
分类名称不能为空。=分类名称不能为空。
分类名称不能重复。=分类名称不能重复。
重命名分类=重命名分类
确认删除分类=确认删除分类
删除非空分类=删除非空分类
确认删除分类“%s”？=确认删除分类“%s”？
分类“%s”包含 %zu 个项目。=分类“%s”包含 %zu 个项目。
移动到=移动到
移动项目并删除分类=移动项目并删除分类
同时删除项目=同时删除项目
)INI";

        constexpr const char* kTraditional = R"INI([strings]
语言=語言
常用=常用
%s；按住 %s 可连续启动并临时置顶=%s；按住 %s 可連續啟動並暫時保持最上層
临时钉住（松开按键后取消置顶）=暫時釘選（放開按鍵後取消最上層）
取消钉住=取消釘選
钉住=釘選
窗口外观=視窗外觀
外观=外觀
分类栏布局=分類列版面配置
呼出与位置=呼出與位置
系统与备份=系統與備份
标题=標題
居中=置中
显示名称=顯示名稱
显示边框=顯示邊框
图标布局=圖示版面配置
图标=圖示
横距=水平間距
纵距=垂直間距
紧凑模式列宽度=精簡模式欄寬
项目启动方式=項目啟動方式
单击启动=按一下啟動
双击启动=按兩下啟動
整体透明度=整體透明度
分类栏位置=分類列位置
布局=版面配置
顶部单行=頂部單行
顶部自动换行=頂部自動換行
左侧=左側
右侧=右側
侧边栏最大宽度=側邊列最大寬度
横向文字=橫向文字
竖向文字=直向文字
文字方向=文字方向
从上到下=從上到下
从下到上=從下到上
阅读方向=閱讀方向
分类切换=分類切換
鼠标点击=滑鼠點擊
鼠标悬停=滑鼠懸停
切换方式=切換方式
悬停延迟（毫秒）=懸停延遲（毫秒）
快捷呼出=快速呼出
启用快捷呼出=啟用快速呼出
启用键盘快捷键=啟用鍵盤快速鍵
启用鼠标快捷键=啟用滑鼠快速鍵
鼠标按键=滑鼠按鍵
键盘快捷键=鍵盤快速鍵
快捷键=快速鍵
请按下快捷键…=請按下快速鍵…
取消录制=取消錄製
录制=錄製
无=無
中键=中鍵
侧键 1=側鍵 1
侧键 2=側鍵 2
触发按键=觸發按鍵
组合键=組合鍵
双击=按兩下
连续启动=連續啟動
临时钉住按键=暫時釘選按鍵
主界面启动位置=主介面啟動位置
屏幕正中=螢幕正中央
屏幕角落=螢幕角落
用户自定义=使用者自訂
鼠标位置=滑鼠位置
位置=位置
左上=左上
右上=右上
左下=左下
右下=右下
使用当前位置=使用目前位置
开机自启=開機自動啟動
自启优先级=自啟優先順序
默认=預設
高=高
极高(不推荐)=極高(不建議)
"高"使用高于正常优先级,"极高"使用高优先级.="高"使用高於正常優先順序,"極高"使用高優先順序.
自动备份=自動備份
份=份
立即备份=立即備份
导出=匯出
导入=匯入
配置目录=設定目錄
关闭=關閉
软件设置=軟體設定
显示 XLaunch=顯示 XLaunch
隐藏 XLaunch=隱藏 XLaunch
作者主页=作者首頁
退出=結束
打开设置=開啟設定
启动=啟動
启动并保持显示=啟動並保持顯示
以管理员身份运行=以系統管理員身分執行
打开所在位置=開啟所在位置
编辑=編輯
复制=複製
移动到分类=移動到分類
删除=刪除
显示方式=顯示方式
图标网格=圖示網格
紧凑列表=精簡清單
新增启动项目=新增啟動項目
添加系统图标=新增系統圖示
新增分类=新增分類
新增=新增
重命名=重新命名
取消=取消
保存=儲存
确定=確定
启动项编辑=啟動項目編輯
编辑启动项目=編輯啟動項目
自定义名称=自訂名稱
目标路径=目標路徑
浏览文件=瀏覽檔案
浏览文件夹=瀏覽資料夾
启动参数=啟動參數
工作目录=工作目錄
自定义图标路径=自訂圖示路徑
默认以管理员身份运行=預設以系統管理員身分執行
全局快捷键=全域快速鍵
软件内快捷键=軟體內快速鍵
清除=清除
顶部单行支持横向滚动；顶部换行会根据窗口宽度自动增加行数。=頂部單行支援水平捲動；頂部換行會依視窗寬度自動增加列數。
按住时可连续启动项目并保持主界面置顶=按住時可連續啟動項目並讓主介面保持最上層
每次呼出时定位到鼠标附近=每次呼出時定位到滑鼠附近
显示在主屏幕工作区域正中=顯示在主螢幕工作區域正中央
分类名称=分類名稱
分类名称不能为空。=分類名稱不能為空。
分类名称不能重复。=分類名稱不能重複。
重命名分类=重新命名分類
确认删除分类=確認刪除分類
删除非空分类=刪除非空分類
确认删除分类“%s”？=確認刪除分類「%s」？
分类“%s”包含 %zu 个项目。=分類「%s」包含 %zu 個項目。
移动到=移動到
移动项目并删除分类=移動項目並刪除分類
同时删除项目=同時刪除項目
)INI";

        constexpr const char* kEnglish = R"INI([strings]
语言=Language
常用=Common
%s；按住 %s 可连续启动并临时置顶=%s; hold %s to launch multiple items and temporarily keep on top
临时钉住（松开按键后取消置顶）=Temporarily pinned (release the key to cancel always-on-top)
取消钉住=Unpin
钉住=Pin and keep visible
窗口外观=Window appearance
外观=Appearance
分类栏布局=Category bar
呼出与位置=Activation & position
系统与备份=System & backup
标题=Title
居中=Center
显示名称=Show names
显示边框=Show borders
图标布局=Icon layout
图标=Icon
横距=Horizontal gap
纵距=Vertical gap
紧凑模式列宽度=Compact column width
项目启动方式=Item activation
单击启动=Single click
双击启动=Double click
整体透明度=Opacity
分类栏位置=Category bar position
布局=Layout
顶部单行=Top single row
顶部自动换行=Top wrapping
左侧=Left
右侧=Right
侧边栏最大宽度=Maximum sidebar width
横向文字=Horizontal text
竖向文字=Vertical text
文字方向=Text direction
从上到下=Top to bottom
从下到上=Bottom to top
阅读方向=Reading direction
分类切换=Category switching
鼠标点击=Mouse click
鼠标悬停=Mouse hover
切换方式=Switch method
悬停延迟（毫秒）=Hover delay (ms)
快捷呼出=Quick activation
启用快捷呼出=Enable quick activation
启用键盘快捷键=Enable keyboard shortcut
启用鼠标快捷键=Enable mouse shortcut
鼠标按键=Mouse button
键盘快捷键=Keyboard shortcut
快捷键=Shortcut
请按下快捷键…=Press a shortcut…
取消录制=Cancel recording
录制=Record
无=None
中键=Middle button
侧键 1=Side button 1
侧键 2=Side button 2
触发按键=Trigger button
组合键=Modifier button
双击=Double click
连续启动=Continuous launch
临时钉住按键=Temporary pin key
主界面启动位置=Main window start position
屏幕正中=Screen center
屏幕角落=Screen corner
用户自定义=Custom
鼠标位置=Mouse position
位置=Position
左上=Top left
右上=Top right
左下=Bottom left
右下=Bottom right
使用当前位置=Use current position
开机自启=Start with Windows
自启优先级=Startup priority
默认=Default
高=High
极高(不推荐)=Very high (not recommended)
"高"使用高于正常优先级,"极高"使用高优先级.=High uses above-normal process priority; Very high uses high priority.
自动备份=Automatic backup
份=copies
立即备份=Back up now
导出=Export
导入=Import
配置目录=Config folder
关闭=Close
软件设置=Settings
显示 XLaunch=Show XLaunch
隐藏 XLaunch=Hide XLaunch
作者主页=Author website
退出=Exit
打开设置=Open settings
启动=Launch
启动并保持显示=Launch and keep visible
以管理员身份运行=Run as administrator
打开所在位置=Open file location
编辑=Edit
复制=Duplicate
移动到分类=Move to category
删除=Delete
显示方式=View mode
图标网格=Icon grid
紧凑列表=Compact list
新增启动项目=Add launch item
添加系统图标=Add system icon
新增分类=Add category
新增=Add
重命名=Rename
取消=Cancel
保存=Save
确定=OK
启动项编辑=Launch item editor
编辑启动项目=Edit launch item
自定义名称=Custom name
目标路径=Target path
浏览文件=Browse file
浏览文件夹=Browse folder
启动参数=Arguments
工作目录=Working directory
自定义图标路径=Custom icon path
默认以管理员身份运行=Run as administrator by default
全局快捷键=Global shortcut
软件内快捷键=In-app shortcut
清除=Clear
顶部单行支持横向滚动；顶部换行会根据窗口宽度自动增加行数。=A top single row scrolls horizontally; wrapping adds rows based on window width.
按住时可连续启动项目并保持主界面置顶=Hold to launch multiple items while keeping the main window on top
每次呼出时定位到鼠标附近=Position near the mouse whenever activated
显示在主屏幕工作区域正中=Center in the primary monitor work area
分类名称=Category name
分类名称不能为空。=Category name cannot be empty.
分类名称不能重复。=Category name must be unique.
重命名分类=Rename category
确认删除分类=Confirm category deletion
删除非空分类=Delete non-empty category
确认删除分类“%s”？=Delete category "%s"?
分类“%s”包含 %zu 个项目。=Category "%s" contains %zu items.
移动到=Move to
移动项目并删除分类=Move items and delete category
同时删除项目=Delete items too
)INI";

        std::string Trim(std::string value)
        {
            const auto first = value.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            const auto last = value.find_last_not_of(" \t\r\n");
            return value.substr(first, last - first + 1);
        }

        void WriteBuiltin(const std::filesystem::path& path, const char* content)
        {
            if (std::filesystem::exists(path)) return;
            std::ofstream output(path, std::ios::binary);
            output.write(content, static_cast<std::streamsize>(std::char_traits<char>::length(content)));
        }

        void RefreshLanguages()
        {
            g_languages.clear();
            std::error_code error;
            for (const auto& entry : std::filesystem::directory_iterator(g_languageDirectory, error))
            {
                if (!entry.is_regular_file() || entry.path().extension() != L".ini") continue;
                const std::u8string stem = entry.path().stem().u8string();
                g_languages.emplace_back(reinterpret_cast<const char*>(stem.data()), stem.size());
            }
            std::sort(g_languages.begin(), g_languages.end());
        }

        bool Load(const std::string& language)
        {
            std::filesystem::path path = g_languageDirectory / (language + ".ini");
            std::ifstream input(path, std::ios::binary);
            if (!input) return false;
            g_strings.clear();
            std::string line;
            bool stringsSection = false;
            while (std::getline(input, line))
            {
                if (line.starts_with("\xEF\xBB\xBF")) line.erase(0, 3);
                line = Trim(line);
                if (line.empty() || line[0] == ';' || line[0] == '#') continue;
                if (line.front() == '[' && line.back() == ']')
                {
                    stringsSection = line == "[strings]";
                    continue;
                }
                if (!stringsSection) continue;
                const std::size_t separator = line.find('=');
                if (separator == std::string::npos) continue;
                g_strings[Trim(line.substr(0, separator))] = Trim(line.substr(separator + 1));
            }
            g_currentLanguage = language;
            return true;
        }
    }

    void LanguageManager::Initialize(const std::filesystem::path& configDirectory, const std::string& language)
    {
        g_languageDirectory = configDirectory / "lang";
        std::error_code error;
        std::filesystem::create_directories(g_languageDirectory, error);
        WriteBuiltin(g_languageDirectory / "zh-cn.ini", kSimplified);
        WriteBuiltin(g_languageDirectory / "zh-tw.ini", kTraditional);
        WriteBuiltin(g_languageDirectory / "en.ini", kEnglish);
        RefreshLanguages();
        if (!Load(language)) Load("zh-cn");
    }

    bool LanguageManager::SetLanguage(const std::string& language)
    {
        RefreshLanguages();
        return Load(language);
    }

    const char* LanguageManager::Get(const char* key)
    {
        const auto found = g_strings.find(key);
        return found == g_strings.end() || found->second.empty() ? key : found->second.c_str();
    }

    std::wstring LanguageManager::GetWide(const char* key)
    {
        const char* text = Get(key);
        const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
        std::wstring result(size > 0 ? size : 0, L'\0');
        if (size > 1) MultiByteToWideChar(CP_UTF8, 0, text, -1, result.data(), size);
        if (!result.empty()) result.pop_back();
        return result;
    }

    const std::vector<std::string>& LanguageManager::AvailableLanguages() { RefreshLanguages(); return g_languages; }
    const std::string& LanguageManager::CurrentLanguage() { return g_currentLanguage; }

    std::string LanguageManager::DetectSystemLanguage()
    {
        const LANGID language = GetUserDefaultUILanguage();
        if (PRIMARYLANGID(language) != LANG_CHINESE)
            return "en";
        const WORD sublanguage = SUBLANGID(language);
        return sublanguage == SUBLANG_CHINESE_TRADITIONAL ||
            sublanguage == SUBLANG_CHINESE_HONGKONG || sublanguage == SUBLANG_CHINESE_MACAU
            ? "zh-tw" : "zh-cn";
    }
}
