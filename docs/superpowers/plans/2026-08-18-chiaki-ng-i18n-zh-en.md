# chiaki-ng 中英文界面 i18n 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为 chiaki-ng Qt GUI 建立标准 Qt 翻译管线，支持简体中文（zh_CN）与英文（en）界面，跟随系统语言 + 设置中手动覆盖，重启生效。

**架构：** 使用 Qt 官方 `qt_add_translations`（Qt 6.2+）接入 lupdate/lrelease，生成 `chiaki_en.ts` 与 `chiaki_zh_CN.ts`，`.qm` 编译进 qrc 实现单文件分发。`main.cpp` 启动时按 `settings/language` 键（`system`/`en`/`zh_CN`）加载对应 `.qm` 并 `installTranslator`。设置对话框新增 Language 下拉框，写入 `Settings`。翻译提取与中文填充由 Python 脚本完成（本机无 Qt 工具链）。

**技术栈：** Qt6 (Quick/QML + C++), CMake, Qt Linguist (.ts/.qm), Python 3.12 (提取脚本), GitHub Actions (构建验证)

**前置事实：**
- 文案规模：QML 609 处 `qsTr()` + C++ 68 处 `tr()` ≈ 680 条
- QML context = 文件名（如 `Main.qml` → context `Main`），C++ context = 类名（lupdate 规则）
- 本机无 cmake/lupdate/Qt，无法本地构建；构建验证依赖 CI（`build-pr.yaml`）或开发机
- 现有模式：`Settings` 用 `QSettings`，键名 `settings/xxx`（`gui/include/settings.h:241` 起）
- 入口：`main.cpp:126` 创建 `QApplication`，`:180` 创建 `Settings`，`:192`/`:291` 调用 `RunMain`/`RunStream`

---

### 任务 1：创建 translations 目录与 CMake 翻译管线

**文件：**
- 创建：`gui/translations/`（目录）
- 修改：`gui/CMakeLists.txt`

- [ ] **步骤 1：创建 translations 目录**

```bash
mkdir gui/translations
```

- [ ] **步骤 2：在 CMakeLists.txt 接入 qt_add_translations**

在 `gui/CMakeLists.txt` 的 `set_target_properties(chiaki ...)`（第 146-154 行）之后、`install(TARGETS chiaki ...)`（第 156 行）之前插入：

```cmake
qt_add_translations(chiaki
	TS_FILES
		${CMAKE_CURRENT_SOURCE_DIR}/translations/chiaki_en.ts
		${CMAKE_CURRENT_SOURCE_DIR}/translations/chiaki_zh_CN.ts
	QM_FILES_OUTPUT_VARIABLE QM_FILES
	RESOURCE_PREFIX "/i18n"
	LUPDATE_OPTIONS --locations relative -no-obsolete
)
target_sources(chiaki PRIVATE ${QM_FILES})
```

说明：`qt_add_translations` 自动收集 `chiaki` target 的源文件（含 `res/resources.qrc`、`src/qml/qml.qrc`，CMakeLists.txt:35,44），lupdate 会从 `.cpp/.h/.qml` 提取 `tr()`/`qsTr()`。`RESOURCE_PREFIX "/i18n"` 使 `chiaki_zh_CN.qm` 编译进 `:/i18n/chiaki_zh_CN.qm`。

- [ ] **步骤 3：Commit**

```bash
git add gui/translations gui/CMakeLists.txt
git commit -m "feat(gui): add Qt translation pipeline for i18n"
```

---

### 任务 2：Settings 增加 language 键

**文件：**
- 修改：`gui/include/settings.h`

- [ ] **步骤 1：添加 Get/SetLanguage**

在 `gui/include/settings.h` 中、`GetVSyncEnabled`/`SetVSyncEnabled`（第 263-264 行）之后添加：

```cpp
		QString GetLanguage() const					{ return settings.value("settings/language", "system").toString(); }
		void SetLanguage(const QString &language)	{ settings.setValue("settings/language", language); }
```

- [ ] **步骤 2：Commit**

```bash
git add gui/include/settings.h
git commit -m "feat(gui): add language setting to Settings"
```

---

### 任务 3：main.cpp 启动时加载翻译

**文件：**
- 修改：`gui/src/main.cpp`

- [ ] **步骤 1：添加 include**

在 `gui/src/main.cpp` 的 `#include <QApplication>`（第 22 行）之后添加：

```cpp
#include <QTranslator>
#include <QLocale>
```

- [ ] **步骤 2：添加翻译安装辅助函数**

在 `int real_main(...)`（第 67 行）之前添加：

```cpp
static void InstallTranslator(QApplication &app, const QString &language)
{
	static QTranslator translator;
	QString lang = language;
	if(lang == "system")
		lang = QLocale::system().language() == QLocale::Chinese ? "zh_CN" : "en";
	if(lang == "zh_CN" && translator.load(":/i18n/chiaki_zh_CN.qm"))
		app.installTranslator(&translator);
}
```

注意：`translator` 用 `static`，保证它在 `app.exec()` 整个生命周期内存活（翻译器必须与 `QApplication` 同生命周期）。`QLocale::Chinese` 涵盖简体与繁体；本项目仅提供 `zh_CN`，繁体中文系统也会加载简体翻译（可接受，后续可扩展）。

- [ ] **步骤 3：在 Settings 创建后调用**

在 `gui/src/main.cpp` 第 184 行（`Settings alt_settings(...)`）之后、第 191 行（`if(args.length() == 0)`）之前插入：

```cpp
	InstallTranslator(app, use_alt_settings ? alt_settings.GetLanguage() : settings.GetLanguage());
```

这样 `--profile`/CLI 与 GUI 两条路径都生效。

- [ ] **步骤 4：Commit**

```bash
git add gui/src/main.cpp
git commit -m "feat(gui): install translator on startup based on language setting"
```

---

### 任务 4：QmlSettings 暴露 language 属性

**文件：**
- 修改：`gui/include/qmlsettings.h`
- 修改：`gui/src/qmlsettings.cpp`

- [ ] **步骤 1：添加 Q_PROPERTY**

在 `gui/include/qmlsettings.h` 中 `Q_PROPERTY(QString logDirectory ...)`（第 70 行）之前添加：

```cpp
	Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
```

- [ ] **步骤 2：声明信号与访问器**

在 `qmlsettings.h` 的 `signals:` 区段找到 `audioDevicesChanged`（第 72 行引用处对应 signal 声明，位于文件后部）附近添加：

```cpp
		void languageChanged();
```

并在类声明中（private 或 public 区）添加：

```cpp
		QString language() const;
		void setLanguage(const QString &language);
```

（与现有 `logVerbose()`/`setLogVerbose()` 同区，参照 `gui/src/qmlsettings.cpp:246-256` 的实现模式。）

- [ ] **步骤 3：实现访问器**

在 `gui/src/qmlsettings.cpp` 中 `bool QmlSettings::logSanitize() const`（第 258 行）之前添加：

```cpp
QString QmlSettings::language() const
{
	return settings->GetLanguage();
}

void QmlSettings::setLanguage(const QString &language)
{
	settings->SetLanguage(language);
	emit languageChanged();
}
```

- [ ] **步骤 4：Commit**

```bash
git add gui/include/qmlsettings.h gui/src/qmlsettings.cpp
git commit -m "feat(gui): expose language setting to QML"
```

---

### 任务 5：SettingsDialog 添加语言下拉框

**文件：**
- 修改：`gui/src/qml/SettingsDialog.qml`

- [ ] **步骤 1：在 General 标签添加语言行**

在 `gui/src/qml/SettingsDialog.qml` 的 `generalLayout` 内 `GridLayout`（第 460 行起，columns: 3）中、`"Action On Disconnect:"` 行（第 466 行）之前添加三列（Label + ComboBox + 说明 Label）：

```qml
Label {
	Layout.alignment: Qt.AlignRight
	text: qsTr("Language:")
}

C.ComboBox {
	Layout.preferredWidth: 400
	model: [qsTr("System"), qsTr("Simplified Chinese"), qsTr("English")]
	currentIndex: {
		const lang = Chiaki.settings.language;
		if(lang === "zh_CN") return 1;
		if(lang === "en") return 2;
		return 0;
	}
	onActivated: index => {
		Chiaki.settings.language = index === 0 ? "system" : (index === 1 ? "zh_CN" : "en");
	}
}

Label {
	Layout.alignment: Qt.AlignRight
	text: qsTr("(Restart required)")
}
```

- [ ] **步骤 2：Commit**

```bash
git add gui/src/qml/SettingsDialog.qml
git commit -m "feat(gui): add language selector to settings dialog"
```

---

### 任务 6：编写翻译提取/填充脚本并生成 .ts 文件

**文件：**
- 创建：`scripts/i18n_extract.py`
- 生成：`gui/translations/chiaki_en.ts`
- 生成：`gui/translations/chiaki_zh_CN.ts`

本任务生成 `.ts` 文件。分两个脚本：`extract`（从源码提取上下文/源字符串，输出两个 `.ts` 骨架）与 `fill`（把中文翻译写进 `chiaki_zh_CN.ts`）。因为本机无 lupdate，用 Python 复刻 lupdate 的 context 划分：QML → 文件名，C++ → 类名。

- [ ] **步骤 1：编写提取脚本**

创建 `scripts/i18n_extract.py`：

```python
#!/usr/bin/env python3
"""Extract translatable strings from chiaki-ng GUI sources into .ts files.

Replicates lupdate's context assignment:
  - QML files: context = file basename (Main.qml -> Main)
  - C++ files: context = enclosing class name
Usage:
  python scripts/i18n_extract.py
Outputs gui/translations/chiaki_en.ts and chiaki_zh_CN.ts (translation placeholders).
"""
import re, sys, os, xml.etree.ElementTree as ET

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GUI = os.path.join(ROOT, "gui")
OUT = os.path.join(GUI, "translations")

QML_DIRS = ["src/qml", "src/qml/controls"]
CPP_DIRS = ["src", "include"]

QSTR = re.compile(r'qsTr\("((?:[^"\\]|\\.)*)"')
TR = re.compile(r'(?<!\w)tr\("((?:[^"\\]|\\.)*)"')
CLASS_RE = re.compile(r'\bclass\s+(\w+)')

def unescape(s):
    return s.replace('\\"', '"').replace("\\\\", "\\").replace("\\n", "\n").replace("\\t", "\t")

def scan_qml():
    messages = {}
    for d in QML_DIRS:
        base = os.path.join(GUI, d)
        for fn in sorted(os.listdir(base)):
            if not fn.endswith(".qml"):
                continue
            ctx = os.path.splitext(fn)[0]
            path = os.path.join(base, fn)
            with open(path, encoding="utf-8") as f:
                src = f.read()
            for m in QSTR.finditer(src):
                messages.setdefault(ctx, set()).add(unescape(m.group(1)))
    return messages

def scan_cpp():
    messages = {}
    for d in CPP_DIRS:
        base = os.path.join(GUI, d)
        if not os.path.isdir(base):
            continue
        for fn in sorted(os.listdir(base)):
            if not (fn.endswith(".cpp") or fn.endswith(".h")):
                continue
            path = os.path.join(base, fn)
            with open(path, encoding="utf-8") as f:
                lines = f.readlines()
            cur_class = ""
            for line in lines:
                cls = CLASS_RE.search(line)
                if cls and ("Q_OBJECT" in "\n".join(lines) or "public" in line):
                    cur_class = cls.group(1)
                for m in TR.finditer(line):
                    msg = unescape(m.group(1))
                    ctx = cur_class or os.path.splitext(fn)[0]
                    messages.setdefault(ctx, set()).add(msg)
    return messages

def write_ts(path, language, messages, source_lang="en"):
    root = ET.Element("TS", {"version": "2.1", "language": language, "sourcelanguage": source_lang})
    for ctx in sorted(messages):
        c = ET.SubElement(root, "context")
        ET.SubElement(c, "name").text = ctx
        for msg in sorted(messages[ctx]):
            m = ET.SubElement(c, "message")
            ET.SubElement(m, "source").text = msg
            ET.SubElement(m, "translation").text = msg if language == source_lang else ""
    tree = ET.ElementTree(root)
    ET.indent(tree, space="\t")
    tree.write(path, encoding="utf-8", xml_declaration=True)

def main():
    os.makedirs(OUT, exist_ok=True)
    messages = scan_qml()
    messages.update(scan_cpp())
    write_ts(os.path.join(OUT, "chiaki_en.ts"), "en", messages, source_lang="en")
    write_ts(os.path.join(OUT, "chiaki_zh_CN.ts"), "zh_CN", messages, source_lang="en")
    total = sum(len(v) for v in messages.values())
    print(f"Extracted {total} messages across {len(messages)} contexts -> {OUT}")

if __name__ == "__main__":
    main()
```

- [ ] **步骤 2：运行提取脚本**

运行：`python scripts/i18n_extract.py`
预期：输出 `Extracted ~680 messages across N contexts -> ...\translations`，生成 `chiaki_en.ts` 与 `chiaki_zh_CN.ts`。

- [ ] **步骤 3：校验提取数量与 Qt 约定一致**

运行（确认无遗漏，对比真实 `tr()`/`qsTr()` 调用数）：
```bash
python -c "import re,glob; qml=sum(len(re.findall(r'qsTr\(\"',open(f,encoding='utf-8').read())) for f in glob.glob('gui/src/qml/**/*.qml',recursive=True)); cpp=sum(len(re.findall(r'(?<!\w)tr\(\"',open(f,encoding='utf-8').read())) for f in glob.glob('gui/src/*.cpp')+glob.glob('gui/src/*.h')); print('QML',qml,'CPP',cpp,'total',qml+cpp)"
```

对比脚本输出的 total。若脚本输出明显偏少（>5%），说明提取正则漏了特殊写法（如 `qsTr` 含参、多行调用），需检查并补正则。预期两侧总量接近。

- [ ] **步骤 4：编写中文翻译填充脚本**

创建 `scripts/i18n_fill.py`。该脚本读取 `chiaki_zh_CN.ts`，对每个 `message/source` 在 `TRANSLATIONS` 字典中查找翻译并写入 `translation` 元素；未命中则保持为空并记录缺失列表，便于逐条补译。

```python
#!/usr/bin/env python3
"""Fill Chinese translations into chiaki_zh_CN.ts.

Lookup dict TRANSLATIONS maps source-en -> zh_CN. Messages not found stay
empty and are reported as MISSING so the operator can fill them in the dict.
Usage: python scripts/i18n_fill.py
"""
import xml.etree.ElementTree as ET
import os, re

TS = os.path.join(os.path.dirname(__file__), "..", "gui", "translations", "chiaki_zh_CN.ts")

# context -> {source_en: zh_CN}
TRANSLATIONS = {
    # "MainView": {
    #     "Quit": "退出",
    # },
}

def percent_tokens(s):
    return sorted(re.findall(r"%(\d+|L\d|Ln|1|2|\S)", s))

def main():
    tree = ET.parse(TS)
    root = tree.getroot()
    missing = []
    for ctx in root.findall("context"):
        cname = ctx.find("name").text
        for msg in ctx.findall("message"):
            src = msg.find("source").text or ""
            trans = msg.find("translation")
            entry = TRANSLATIONS.get(cname, {}).get(src)
            if entry is None:
                missing.append(f"{cname}: {src}")
                continue
            if percent_tokens(entry) != percent_tokens(src):
                print(f"WARN placeholder mismatch [{cname}] {src!r} -> {entry!r}")
            trans.text = entry
    ET.indent(tree, space="\t")
    tree.write(TS, encoding="utf-8", xml_declaration=True)
    if missing:
        print(f"MISSING ({len(missing)}):")
        for m in missing:
            print("  ", m)
    else:
        print("All messages translated.")

if __name__ == "__main__":
    main()
```

- [ ] **步骤 5：填充完整中英翻译字典**

将约 680 条英文源字符串逐条翻译为简体中文，填入 `scripts/i18n_fill.py` 的 `TRANSLATIONS` 字典。翻译规范：

- 保留占位符：`%1`、`%2` 等顺序不变；含 `<b>`、`&`、`\n` 的保留原样
- 界面术语一致性（PS 术语用官方中文）：PlayStation/PS4/PS5/Remote Play 保留英文；"Stream Session"→"串流会话"；"Console"→"主机"；"Wake Up"→"唤醒"；"Registered"→"已注册"；"Controller"→"手柄"；"Settings"→"设置"
- 按钮级短文本简短直译，对话说明句完整通顺
- 每条翻译与 `%1`/`%2` 占位符位置一一对应（脚本会 WARN 不匹配项）
- QML 中同文本但不同 context（如 `RemindDialog` 与 `SteamShortcutDialog` 共用"Remote Play via PSN"）各自单独翻译

- [ ] **步骤 6：运行填充脚本并修复 WARN**

运行：`python scripts/i18n_fill.py`
预期：输出 `All messages translated.`，无 MISSING、无 WARN。若有 MISSING，补全字典后重跑；若有 placeholder WARN，修正对应译文后重跑。

- [ ] **步骤 7：校验 .ts 文件格式**

运行（用 Python 验证 XML 合法性 + 统计）：
```bash
python -c "import xml.etree.ElementTree as ET; t=ET.parse('gui/translations/chiaki_zh_CN.ts'); msgs=t.getroot().findall('.//message'); empty=[m.findtext('source') for m in msgs if not (m.findtext('translation') or m.find('translation').get('type')=='unfinished' and False)]; print('total',len(msgs),'contexts',len(t.getroot().findall('context')),'empty_translations',len(empty))"
```
预期：`total` 与任务 6 步骤 2 输出一致，`empty_translations` 为 0。

- [ ] **步骤 8：Commit**

```bash
git add scripts/i18n_extract.py scripts/i18n_fill.py gui/translations
git commit -m "feat(gui): add Simplified Chinese translations (zh_CN) for GUI"
```

---

### 任务 7：构建验证

**文件：** 无（仅验证）

本机无 cmake/Qt/lupdate，无法本地构建。验证策略：CI + 可选开发机复验。

- [ ] **步骤 1：确认 CI 覆盖**

确认 `build-pr.yaml` 存在且会构建 GUI（该 workflow 构建 Windows 目标，CMake 构建 `chiaki` target 时 `qt_add_translations` 会调用 lupdate/lrelease 生成 `.qm`）。若 PR 构建通过，说明 CMake 管线正确。

- [ ] **步骤 2：（可选）开发机复验**

在有 Qt 6.2+ 与 cmake 的环境运行：
```bash
cmake -B build -DCHIAKI_ENABLE_CLI=OFF
cmake --build build --target chiaki
```
预期：构建成功，`build/gui` 下生成含 `chiaki_zh_CN.qm` 的资源。

- [ ] **步骤 3：手动冒烟测试（需运行环境）**

1. 中文系统下启动 chiaki-ng（`settings/language` 未设置或为 `system`）→ 界面显示简体中文
2. 设置 → General → Language 选 "English" → 重启 → 界面显示英文
3. 选回 "Simplified Chinese" → 重启 → 界面显示中文
4. `--profile=test` 启动，确认翻译同样生效

---

### 规格覆盖自检

- [x] 需求 1（跟随系统 + 手动覆盖）→ 任务 3（`system` 逻辑）+ 任务 5（下拉框）
- [x] 需求 2（en.ts + zh_CN.ts 双文件）→ 任务 1/6
- [x] 需求 3（重启生效）→ 任务 3（启动时加载，无动态重载）+ 任务 5（"(Restart required)"提示）
- [x] 需求 4（完整翻译 680 条）→ 任务 6
