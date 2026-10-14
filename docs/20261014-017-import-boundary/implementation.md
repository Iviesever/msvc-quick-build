# 017：Python 预加载后的导入边界（实现子项，未准入正式观察）

日期：2026-10-14。接续 feature 的 `09da916838a242a9379c0e13b1245ff4c9e20cda`，
而不是旧 #217 或已消耗的016。只增加一个实现模块、一个测试模块和本文。
原五个017源码/测试文件逐字保持；没有产品、工作流、权限或VERSION变更。

## 完成的实现与未完成的边界

`tests/native/bcrypt_import_boundary_017.py` 提供预加载结束后的单次导入边界。
它要求调用者提供完整的预期模块元数据，并检查模块缓存与查找器是否保持不变。
它不是正式103B owner，不选择目标、不读宿主文件、不运行loader/decoder，
不签署独立审阅，也不把元数据路径或对象身份等同于已加载代码的字节身份。

现有ELF库存只能说明静态声明关系；本组件补充的是Python层的一部分动态限制。
**实际loader、原生扩展内部dlopen、Python启动/预加载字节和完整owner路径仍未验证。**
完整017未完成，目标SHA256继续为null，本文不分配任何准备或正式观察槽位。

## 调用与行为

```python
# expected必须来自另行审阅/冻结的完整声明，不得根据现场缓存自动“放行”。
boundary = ImportBoundary(expected)
boundary.activate()
# owner在读取新目标、预留decoder调用及完成每个阶段时做checkpoint。
# 此片段不执行任何这些动作，也不表示其已获准。
boundary.checkpoint()
result = boundary.close()
```

每个模块声明必须恰含 `origin/file/cached/package/spec_name/search/path` 七项。
支持显式文件来源、built-in和frozen；对无来源namespace、自定义spec/模块对象、
正在初始化的模块、相对路径、非法字段及超限输入保持拒绝。`__main__`无spec时
也必须声明绝对源文件路径。使用模块的字典直接读取元数据，不触发`__getattr__`。
这些限制是此组件的窄范围，不是通用Python加载器兼容性承诺。

`activate()`在完整集合匹配后才安装首位meta-path finder。它保存模块、spec和
loader的强引用以及查找器原顺序，不仅保存可复用的id数字。之后所有需要进入
查找协议的导入/重载都直接抛错，不返回None让后续finder继续加载。

Python先查`sys.modules`，因此缓存命中可以绕过finder。`checkpoint()`同时检查：
完整名字集合、原缓存字典身份、各模块/spec/loader对象身份、文件/缓存/package/
package搜索位置等声明、finder列表对象及顺序。检测到变化后首次原因永久保留；
即使调用方吞掉异常或恢复缓存，`close()`仍返回refused。

`close()`只移除自己的finder，不覆盖其他组件对meta-path的变更。无法有界确认
移除时返回`finder_detached=false`，调用方必须停止隔离owner，不能继续观察。
组件对象不能重复激活/关闭。它不是跨对象、跨进程或跨会话的预算/唯一调用登记。

## 资源与信任假设

每份完整声明最多256个模块，每串4096字符，合计131072个文本字符；每个模块的
search/path最多16项，原finder最多16个。先检查容器/长度再复制条目。
这些是对象/文本的逻辑边界，不是解释器RSS、调用方预分配内存、CPU或硬时限保证。
组件本身不创建线程/进程，不枚举目录、读取/proc、哈希文件或复制系统二进制。

使用环境必须是受控、可信、单线程owner。它不是安全沙箱，不监控任意模块属性/
函数代码替换，不监控原生动态加载，也不能发现两个checkpoint之间“注入再移除”
的缓存变化。测试明确保留后一种正控制，禁止宣称已发现这种未观察到的操作。
没有audit-hook或操作系统加载事件覆盖；`-I/-S`本身也不会补足这些证据。

普通结尾`closed_unchanged`仅说明已执行检查未发现差异。结果中的execution、
independent-review、runtime-loader、module-bytes、full-dynamic-import资格全部false。
错误正文不包含模块名、宿主路径、完整清单或系统文件内容。输出可以保留非敏感
状态；以前被拦截的宿主清单不通过本实现重建/改编码/换接口交付。

## 本轮验证

新模块28项测试，原63项测试逐字保持并一起执行：**91 PASS / 0 FAIL / 0 SKIP**。
最初25项通过后增加三项清理/解释器恢复控制及显式finder_detached字段；两份日志
保留。全部是内存对象和原有临时文件系统测试，不运行GNU、loader、Windows或MQB。

三类控制短暂接入当前Python真实导入协议，只使用唯一合成模块名：未知导入在
首次finder处拒绝、reload拒绝、已缓存合成模块直接返回而绕过finder。finally恢复
测试自身改动，没有搜索实际目标模块文件；测试用例自身不启动子解释器或第三方进程。
其余导入状态测试使用显式内存runtime替身，不能据此称实际owner已经合格。

原四项ELF/搜索复核修订仍保留；本次没有修改它们或重新调查宿主库目录。既有
46项独立预检未在本轮运行；不能把91说成全仓库、Windows或CI通过。

## 当前加载验证方案和下一实现边界

正式owner接入仍须逐项完成，顺序不能被这个组件的绿色测试替代：

1. 在允许的私有证据范围内冻结源码、Python/decoder/loader、原生依赖、Python
   源码/pyc/扩展和启动策略。保留静态多提供者拒绝，不能任意选择某个目录放行。
2. 原生加载器初始解析与后续dlopen分开验收；完整合成owner路径的实际加载/导入
   观察须另有准确工具/argv/限额和独立审阅。模块元数据、文件前后哈希、/proc映射
   的局部快照或静态DT_NEEDED均不能单独宣称完整后续加载已证明。
3. 用独立冻结的完整模块声明激活本组件；真实读取/decoder/解析/失败保存路径都
   必须覆盖checkpoint。完整owner负责预加载所有成功和异常分支所需模块，不能
   遇到拒绝就临时追加允许项、重新预加载或重试本次观察。
4. 复用已核对的016 owned-output/唯一调用/失败保留思路及017五份固定元数据，
   实现103B专用选择与执行。只能接受两份准备一致的目标、原图像、pdata/unwind；
   不接受任意范围或替代目标，不跟随静态引用执行机器码。实际源码尚未完成。
5. 完整外层必须把body异常、导入拒绝、child回收、输出持久化和clean owner exit
   一起判定；`boundary.close()`成功不能掩盖body失败。CPU/地址空间/墙钟/输出和
   外层清理预算在完整实现上审阅冻结，不照抄016资源证明。

**本次完成的是Python预加载后的导入边界实现子项，不是独立审阅签署或正式owner。**
下一项先交叉审阅这个待提交的三文件补丁，再接入上述实际owner/加载验收；当前
会话未提供第二位独立审阅者。017完整准备/执行登记仍未形成，不重跑016、不跳018。

## 依据

Python import reference（cache优先、meta-path、reload和可写模块缓存）：
https://docs.python.org/3/reference/import.html

Python sys reference（meta_path/modules的用途与audit hook非安全沙箱限制）：
https://docs.python.org/3/library/sys.html

依据用于说明语言语义；不替代本宿主、本源码和正式owner的实际资格。
