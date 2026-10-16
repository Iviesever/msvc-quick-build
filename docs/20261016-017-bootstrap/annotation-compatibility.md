# 017：去除仅供注解使用的 typing 运行期依赖

基线：`24810b55dfc946d979df3f1ed790cef8d44d2e93`。接续 #198 / 6209113256。
本项只解决 dev006 已证明的 `typing.io` / `typing.re` 模块对象兼容性，
不重写采集器、guard、bootstrap、传输层或元数据校验。

## 原因与修改

`bcrypt_preflight_017.py` 是本次预加载工作集唯一直接导入 typing 的项目文件。
它已经使用 postponed annotations；Any/Mapping 的13处名称引用全部位于注解。
两个入口经 admission contract 引入 preflight，因此都带入 Python3.12 typing 的
兼容命名空间。dev006 已保存其非 ModuleType 事实；它们不是恶意注入的证据。

只修改这份生产文件的8行：Mapping 改从 collections.abc 导入，Any 注解改为
内置 object。所有条件、异常、类型严格比较、五份 PINS、目标几何及返回值保持；
规范化的去注解 AST 也必须匹配固定基线。没有删除 sys.modules 条目、过滤观察值、
重命名命名空间、允许任意对象，或通过升级解释器躲过原环境的问题。

object 与 Any 的静态类型含义并不等价：object 要求静态使用处先收窄类型，故这也
收紧了注解承诺，而不是声称类型检查器结果完全相同。当前没有运行 mypy/pyright；
真实动态校验语句未改。函数注解仍可用 inspect.get_annotations 求值，不留悬空 Any。

## 验证契约

- 原335项测试文件保持，新增10项：typing 导入请求拒绝、运行逻辑AST保持、注解
  可求值、原严格guard对非模块对象继续拒绝、未授权预检/真假数值/坏JSON继续拒绝。
- 新反例在原实现得到3项 FAIL；保留旧源码、测试和日志，修订本地345项通过。
- 五份实际历史JSON，共318238 B，未经修改PINS在旧/新预检获得完全相同返回。
  此检查不是目标准备，实际目标摘要仍null；原历史内容不提交公开仓库。
- 普通bootstrap CI加入10项，319项；只明确替换改变的preflight源码pin，保留
  原backend修复pin及其它pin。已消耗的dev004/005/006文件不修改、不再执行。
- 独立dev007验证345项及两个既有collector角色。必须使用同dev006版本和二进制：
  `/usr/bin/python3`，3.12.3，SHA256
  `e50d468e8b0adfb05733f5b87b3cff34829c4a8c1aea50c865aa8bdfe4bb150f`。
  不匹配就拒绝，不现场替换。两角色均须blockers=[]、未导入typing及其子名、映射
  明确EOF并找到预期PT_INTERP。不是只根据本地Python3.13的成功推测3.12通过。

新登记限一次hosted job/run_number1/attempt1，准确parent与36个源码blob。
单测解释器限60秒，再至多两个collector，各50秒、原有读量上限；3分钟step/5分钟job。
沿用旧collector精确源码，失败保存且不重试；不执行owner/controller主体、decoder、
目标字节或旧scope smoke。没有目标资格、writer authority或自动生成的allowlist。

collector仍是观察器，不是正式bootstrap模块集；干净的兼容性结果不代表完整加载
闭集、所有未来导入、非后代写者或FD外传均已获资格。它也不向未来临时runner自动
授权。此修订由当前会话实现/测试，不冒充第二位独立审阅者签署。

## 依据

- Python3.12 typing文档：Any与object语义、标准集合别名及typing.io/re弃用。
  https://docs.python.org/3.12/library/typing.html
- 标准Mapping接口可直接从collections.abc取得：
  https://docs.python.org/3.12/library/collections.abc.html

实际结果须以新run和原件核对为准。兼容性收口后继续绑定已观察运行文件与真正的
准备/资格材料，不重复改写guard或把“无blocker”升级成正式103B观察放行。
