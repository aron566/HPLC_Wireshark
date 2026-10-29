# trunc-stats-probe（原名 trunc_stats_probe）

NW 截断/统计探针：3 个 .pro（`probe`、`probe_merged`、`probe_sf`），
共用 TARGET `trunc_stats_probe`，靠各自 `build/<name>/` 子目录隔离。

## 构建与运行

```
./build.sh
./build/<name>/trunc_stats_probe [文件…]
```

输入为外部文件（未入库）。无参运行请重定向 stdin：

```
./build/probe/trunc_stats_probe </dev/null
```

注意：无参且 stdin 打开时，首个二进制会阻塞等待 stdin
（归集验证时实测挂起后 kill）；stdin 关闭后 exit 0 静默。
