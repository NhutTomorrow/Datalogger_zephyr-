#!/usr/bin/env python3
"""
Codebase Architecture & Gravity Graph Analyzer for Datalogger Zephyr Project.
Parses C/H files, computes dependency graphs, detects architectural layer violations,
and generates the interactive HTML Gravity View.
"""

import os
import re
import sys
import json
from pathlib import Path

WORKSPACE_DIR = Path(__file__).resolve().parent.parent
SRC_DIR = WORKSPACE_DIR / "src"

LAYER_ORDER = {
    "Core / Entry": 4,
    "Application (FSM)": 3,
    "Services (Middleware)": 2,
    "BSP (Drivers)": 1,
    "Core / Contracts": 0
}

LAYER_COLORS = {
    "Core / Entry": "#ef4444",          # Red
    "Application (FSM)": "#f59e0b",     # Amber / Orange
    "Services (Middleware)": "#3b82f6",  # Blue
    "BSP (Drivers)": "#10b981",          # Emerald / Green
    "Core / Contracts": "#a855f7"        # Purple
}

CONTRACT_FILES = {"channel_config.h", "channel_driver_ops.h", "fault_codes.h"}

def classify_layer(filepath: str) -> str:
    norm = filepath.replace("\\", "/")
    fname = os.path.basename(norm)
    if fname in CONTRACT_FILES:
        return "Core / Contracts"
    elif "/app/" in norm:
        return "Application (FSM)"
    elif "/services/" in norm:
        return "Services (Middleware)"
    elif "/bsp/" in norm:
        return "BSP (Drivers)"
    return "Core / Entry"

def get_architectural_role(filename: str) -> str:
    roles = {
        "main.c": "Điểm khởi tạo hệ thống, cấp phát static context, chạy FSM step loop và toggle debug LED 500ms.",
        "app_fsm.c": "Finite State Machine 11 trạng thái điều phối toàn bộ UI LCD, keypad multi-tap, popup cảnh báo và lưu trữ.",
        "app_fsm.h": "Định nghĩa các enum fsm_state_t, cấu trúc context app_context_t và dependencies.",
        "channel_manager.c": "Điều phối 18 kênh đo, Flash Persistence Append-Only Rolling Slot (Sector 11 128KB), chu kỳ lấy mẫu tick.",
        "channel_manager.h": "API và dữ liệu trung tâm quản lý toàn bộ kênh cảm biến.",
        "channel_hw_caps.c": "Ma trận tra cứu khả năng phần cứng (ADC, 4-20mA, 0-10V, DIN, DOUT, RS485) của 18 kênh vật lý.",
        "channel_hw_caps.h": "Định nghĩa struct cấu hình khả năng phần cứng của kênh.",
        "driver_registry.c": "Bảng điều phối con trỏ hàm (driver_ops_t) decoupled kênh đo với driver BSP tương ứng.",
        "driver_registry.h": "Định nghĩa struct driver_ops_t và prototype đăng ký driver phần cứng.",
        "fault_manager.c": "Đăng ký, lọc debounce, phân cấp độ nghiêm trọng (INFO->CRITICAL) và theo dõi lỗi hệ thống.",
        "fault_manager.h": "API và mã lỗi chuẩn hóa toàn hệ thống (FAULT_CODE_... và FAULT_SEV_...).",
        "keypad_multitap.c": "Động cơ gõ phím Multi-tap ITU E.161 (Numeric 0-delay, Text mode 700ms commit, Nav keys).",
        "keypad_multitap.h": "Cấu hình bàn phím 4x4, timeout commit ký tự và callback nhận phím.",
        "button_manager.c": "Bộ lọc debounce phím bấm, bắt sự kiện nhấn giữ (Long Press) và nhấn nhả (Short Press).",
        "button_manager.h": "Quản lý trạng thái logic 16 nút bấm từ phần cứng.",
        "fault_codes.h": "Từ điển mã lỗi chuẩn hóa toàn hệ thống (Pure Contract).",
        "channel_driver_ops.h": "Hợp đồng giao diện điều khiển phần cứng chuẩn channel_driver_ops_t (Pure Contract).",
        "com_protocol.c": "Parser gói tin UART với ESP32 / Gateway (AI Config Proposal, Telemetry uplink, Frame CRC).",
        "com_protocol.h": "Định nghĩa cấu trúc khung truyền, opcode và parser state machine cho UART6.",
        "channel_config.c": "Hiện thực các hàm phụ trợ chuyển đổi chuỗi / enum cho cấu hình kênh.",
        "channel_config.h": "Khai báo các struct cấu hình kênh ChannelConfig_t, chế độ kênh và dải đo.",
        "bsp_lcd_zephyr.c": "Driver màn hình màu 8080 FSMC 16-bit, double-buffering / dirty-rect redraw, render font.",
        "bsp_lcd.h": "API vẽ đồ họa mức cơ sở: vẽ chuỗi, vẽ hộp thoại, đổ màu, cập nhật vùng dirty.",
        "lcd_font.c": "Bảng font bitmap ASCII 8x16 và 12x24 hỗ trợ vẽ ký tự trên LCD.",
        "lcd_font.h": "Khai báo dữ liệu mảng font ký tự bitmap.",
        "bsp_adc_zephyr.c": "Driver đọc điện áp STM32 ADC1, chuyển đổi mV, kiểm tra đứt dây / clipping.",
        "bsp_adc.h": "Giao diện phần cứng ADC cho CH01..CH07.",
        "bsp_rs485_zephyr.c": "Driver RS485 Modbus-RTU Master nửa song công (Half-duplex) điều khiển chân RE/DE.",
        "bsp_rs485.h": "Giao diện truyền nhận RS485 / Modbus-RTU cho kênh mở rộng.",
        "bsp_din_zephyr.c": "Driver đọc GPIO ngõ vào số DIN và đếm xung qua Interrupts.",
        "bsp_din.h": "Giao diện ngõ vào số và đếm xung tần số cao.",
        "bsp_dout_zephyr.c": "Driver điều khiển Relay / DOUT kèm kiểm tra trạng thái hồi tiếp phần cứng.",
        "bsp_dout.h": "Giao diện điều khiển rơ-le / ngõ ra số.",
        "bsp_button_zephyr.c": "Driver đọc IC dịch 74HC165 qua giao thức SPI cho bàn phím 16 nút.",
        "bsp_button.h": "Giao diện đọc trạng thái thô 16 bit từ 74HC165.",
        "bsp_uart_zephyr.c": "Driver USART6 kết nối ESP32/AI sử dụng hàng đợi k_msgq trong Zephyr.",
        "bsp_uart.h": "Giao diện UART không khóa với ring buffer / Zephyr message queue."
    }
    return roles.get(filename, "Module thành phần trong hệ thống Datalogger.")

def analyze_codebase():
    c_files = list(SRC_DIR.rglob("*.c"))
    h_files = list(SRC_DIR.rglob("*.h"))
    all_files = sorted(c_files + h_files)

    nodes = []
    node_map = {}
    edges = []
    violations = []
    
    total_loc = 0
    layer_stats = {k: {"files": 0, "loc": 0} for k in LAYER_ORDER}

    for f in all_files:
        rel_path = f.relative_to(WORKSPACE_DIR).as_posix()
        fname = f.name
        content = f.read_text(encoding="utf-8", errors="ignore")
        lines = content.splitlines()
        loc = len(lines)
        total_loc += loc
        
        layer = classify_layer(rel_path)
        layer_stats[layer]["files"] += 1
        layer_stats[layer]["loc"] += loc

        local_incs = re.findall(r'#include\s+\"([^\"]+)\"', content)
        sys_incs = re.findall(r'#include\s+<([^>]+)>', content)
        funcs = re.findall(r'^[a-zA-Z0-9_]+\s+([a-zA-Z0-9_]+)\s*\([^\)]*\)\s*\{', content, re.MULTILINE)

        node = {
            "id": fname,
            "path": rel_path,
            "layer": layer,
            "color": LAYER_COLORS[layer],
            "layer_rank": LAYER_ORDER[layer],
            "loc": loc,
            "local_incs": [Path(inc).name for inc in local_incs],
            "sys_incs": sys_incs,
            "func_count": len(funcs),
            "funcs": funcs[:12],
            "role": get_architectural_role(fname),
            "in_edges": [],
            "out_edges": []
        }
        nodes.append(node)
        node_map[fname] = node

    for node in nodes:
        src_name = node["id"]
        src_rank = node["layer_rank"]

        for inc_name in node["local_incs"]:
            if inc_name in node_map:
                tgt_node = node_map[inc_name]
                tgt_rank = tgt_node["layer_rank"]

                node["out_edges"].append(inc_name)
                tgt_node["in_edges"].append(src_name)

                is_violation = src_rank < tgt_rank
                edge = {
                    "source": src_name,
                    "target": inc_name,
                    "is_violation": is_violation
                }
                edges.append(edge)
                if is_violation:
                    violations.append({
                        "from": src_name,
                        "from_layer": node["layer"],
                        "to": inc_name,
                        "to_layer": tgt_node["layer"],
                        "reason": f"Driver tầng thấp ({node['layer']}) đang include trực tiếp Service tầng cao ({tgt_node['layer']}). Cần đảo chiều phụ thuộc qua Interface/Header trung gian."
                    })

    return {
        "nodes": nodes,
        "edges": edges,
        "violations": violations,
        "total_loc": total_loc,
        "layer_stats": layer_stats
    }

def generate_gravity_html(data):
    json_data = json.dumps(data, ensure_ascii=False, separators=(",", ":"))
    
    html = f"""<!DOCTYPE html>
<html lang="vi" class="dark">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Datalogger Zephyr - Codebase Gravity View & Architecture Dashboard</title>
  <script src="https://www.gstatic.com/antigravity/web/dev/tailwindcss.min.js"></script>
  <style>
    body {{
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
      overflow: hidden;
      user-select: none;
    }}
    .custom-scrollbar::-webkit-scrollbar {{
      width: 6px;
      height: 6px;
    }}
    .custom-scrollbar::-webkit-scrollbar-track {{
      background: rgba(30, 41, 59, 0.5);
    }}
    .custom-scrollbar::-webkit-scrollbar-thumb {{
      background: #475569;
      border-radius: 3px;
    }}
    .custom-scrollbar::-webkit-scrollbar-thumb:hover {{
      background: #64748b;
    }}
    @keyframes pulse-ring {{
      0% {{ transform: scale(0.95); opacity: 0.8; }}
      50% {{ transform: scale(1.15); opacity: 0.3; }}
      100% {{ transform: scale(0.95); opacity: 0.8; }}
    }}
    .pulsing-alert {{
      animation: pulse-ring 2s infinite ease-in-out;
    }}
  </style>
</head>
<body class="bg-slate-950 text-slate-100 h-screen w-screen flex flex-col antialiased">

  <!-- TOP HEADER -->
  <header class="h-14 border-b border-slate-800 bg-slate-900/90 backdrop-blur px-5 flex items-center justify-between z-20 shrink-0">
    <div class="flex items-center gap-3">
      <div class="h-8 w-8 rounded-lg bg-gradient-to-tr from-cyan-600 to-emerald-500 flex items-center justify-center font-bold text-white shadow-lg shadow-emerald-500/20">
        G
      </div>
      <div>
        <h1 class="text-sm font-semibold tracking-wide flex items-center gap-2">
          <span>DATALOGGER ZEPHYR</span>
          <span class="text-xs px-2 py-0.5 rounded-full bg-cyan-950 text-cyan-400 border border-cyan-800/60 font-mono">STM32F407</span>
          <span class="text-xs text-slate-400 font-normal">| Codebase Review & Gravity View</span>
        </h1>
      </div>
    </div>

    <!-- TABS NAVIGATION -->
    <div class="flex items-center bg-slate-950/80 p-1 rounded-lg border border-slate-800 text-xs font-medium">
      <button onclick="switchTab('gravity')" id="tab-btn-gravity" class="tab-btn px-3 py-1.5 rounded-md bg-cyan-600 text-white font-semibold transition-all">
        🪐 Gravity Graph
      </button>
      <button onclick="switchTab('layers')" id="tab-btn-layers" class="tab-btn px-3 py-1.5 rounded-md text-slate-400 hover:text-white transition-all">
        🧱 Architecture Layers
      </button>
      <button onclick="switchTab('fsm')" id="tab-btn-fsm" class="tab-btn px-3 py-1.5 rounded-md text-slate-400 hover:text-white transition-all">
        ⚡ FSM State Machine (11 States)
      </button>
      <button onclick="switchTab('audit')" id="tab-btn-audit" class="tab-btn px-3 py-1.5 rounded-md text-slate-400 hover:text-white transition-all flex items-center gap-1.5">
        <span>⚠️ Code Debt & Inversions</span>
        <span class="px-1.5 py-0.2 rounded-full bg-rose-900/80 text-rose-300 font-mono text-[10px]" id="violation-badge">9</span>
      </button>
    </div>

    <!-- QUICK METRICS -->
    <div class="flex items-center gap-4 text-xs font-mono">
      <div class="text-right">
        <div class="text-slate-400 text-[10px] uppercase tracking-wider">Total LOC</div>
        <div class="text-emerald-400 font-bold" id="metric-loc">8,094 lines</div>
      </div>
      <div class="h-6 w-px bg-slate-800"></div>
      <div class="text-right">
        <div class="text-slate-400 text-[10px] uppercase tracking-wider">Files / Modules</div>
        <div class="text-cyan-400 font-bold" id="metric-files">35 files</div>
      </div>
    </div>
  </header>

  <!-- MAIN CONTENT AREA -->
  <main class="flex-1 relative overflow-hidden flex">

    <!-- ================= TAB 1: GRAVITY GRAPH VIEW ================= -->
    <section id="view-gravity" class="tab-content absolute inset-0 flex">
      <!-- CANVAS WRAPPER -->
      <div class="flex-1 relative bg-slate-950 overflow-hidden" id="canvas-container">
        <canvas id="gravity-canvas" class="w-full h-full cursor-grab active:cursor-grabbing block"></canvas>

        <!-- FLOATING CONTROL TOOLBAR -->
        <div class="absolute top-4 left-4 z-10 flex flex-col gap-2">
          <!-- SEARCH & FILTER -->
          <div class="bg-slate-900/90 backdrop-blur border border-slate-800 rounded-xl p-2.5 shadow-2xl flex items-center gap-2">
            <svg class="w-4 h-4 text-slate-400 shrink-0" fill="none" stroke="currentColor" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z"></path>
            </svg>
            <input type="text" id="node-search" placeholder="Tìm file hoặc hàm (app_fsm, adc, flash)..." 
                   class="bg-slate-950 border border-slate-700/80 rounded-lg px-2.5 py-1 text-xs text-slate-200 placeholder-slate-500 w-64 focus:outline-none focus:border-cyan-500 font-mono">
            <button onclick="clearSearch()" class="text-slate-500 hover:text-slate-300 text-xs px-1">✕</button>
          </div>

          <!-- LAYER FILTER BUTTONS -->
          <div class="bg-slate-900/90 backdrop-blur border border-slate-800 rounded-xl p-2 shadow-2xl flex flex-wrap gap-1.5 max-w-sm text-[11px]">
            <button onclick="toggleLayerFilter('All')" id="btn-filter-All" class="filter-layer-btn px-2 py-1 rounded bg-slate-800 text-white font-medium border border-slate-700">All</button>
            <button onclick="toggleLayerFilter('Core / Entry')" id="btn-filter-Core" class="filter-layer-btn px-2 py-1 rounded text-red-400 bg-red-950/30 border border-red-900/40">Core</button>
            <button onclick="toggleLayerFilter('Application (FSM)')" id="btn-filter-App" class="filter-layer-btn px-2 py-1 rounded text-amber-400 bg-amber-950/30 border border-amber-900/40">App (FSM)</button>
            <button onclick="toggleLayerFilter('Services (Middleware)')" id="btn-filter-Services" class="filter-layer-btn px-2 py-1 rounded text-blue-400 bg-blue-950/30 border border-blue-900/40">Services</button>
            <button onclick="toggleLayerFilter('BSP (Drivers)')" id="btn-filter-BSP" class="filter-layer-btn px-2 py-1 rounded text-emerald-400 bg-emerald-950/30 border border-emerald-900/40">BSP</button>
            <button onclick="toggleLayerFilter('Core / Contracts')" id="btn-filter-Contracts" class="filter-layer-btn px-2 py-1 rounded text-purple-400 bg-purple-950/30 border border-purple-900/40">Contracts</button>
          </div>

          <!-- PHYSICS CONTROLS -->
          <div class="bg-slate-900/90 backdrop-blur border border-slate-800 rounded-xl p-2.5 shadow-2xl flex items-center gap-3 text-xs">
            <button onclick="toggleSimulation()" id="btn-pause-sim" class="px-2.5 py-1 rounded bg-slate-800 hover:bg-slate-700 text-slate-200 border border-slate-700 text-[11px] font-mono">
              ⏸ Freeze
            </button>
            <button onclick="resetPhysicsLayout()" class="px-2.5 py-1 rounded bg-slate-800 hover:bg-slate-700 text-slate-200 border border-slate-700 text-[11px] font-mono">
              ↺ Reset Grid
            </button>
            <div class="flex items-center gap-1.5 text-slate-400 text-[11px]">
              <span>Zoom:</span>
              <button onclick="zoomBy(1.2)" class="px-1.5 py-0.5 rounded bg-slate-800 hover:text-white">+</button>
              <button onclick="zoomBy(0.8)" class="px-1.5 py-0.5 rounded bg-slate-800 hover:text-white">-</button>
              <button onclick="resetZoom()" class="px-1.5 py-0.5 rounded bg-slate-800 hover:text-white">1:1</button>
            </div>
          </div>
        </div>

        <!-- LEGEND & HELPER -->
        <div class="absolute bottom-4 left-4 z-10 bg-slate-900/85 backdrop-blur border border-slate-800/80 rounded-xl p-3 shadow-2xl text-[11px] flex flex-col gap-1.5 pointer-events-none">
          <div class="font-semibold text-slate-300 text-xs flex items-center gap-1.5">
            <span>Chú giải đồ thị Trọng lực (Gravity View)</span>
          </div>
          <div class="flex items-center gap-2">
            <span class="w-3 h-3 rounded-full bg-amber-500 inline-block shadow"></span>
            <span class="text-slate-300">Kích thước nốt = Tỷ lệ LOC (File càng to, trọng lực càng lớn)</span>
          </div>
          <div class="flex items-center gap-2">
            <span class="w-4 h-0.5 bg-slate-500 inline-block"></span>
            <span class="text-slate-400">Đường xám: Include hợp lệ (#include từ tầng trên xuống tầng dưới)</span>
          </div>
          <div class="flex items-center gap-2">
            <span class="w-4 h-0.5 border-t-2 border-dashed border-rose-500 inline-block"></span>
            <span class="text-rose-400 font-semibold">Đường đỏ nét đứt: Vi phạm đảo ngược phụ thuộc (BSP -> Service)</span>
          </div>
          <div class="text-[10px] text-slate-500 pt-1">
            * Kéo nốt để ghim vị trí. Lăn chuột để phóng to/thu nhỏ. Click nốt để soi cấu trúc chi tiết.
          </div>
        </div>
      </div>

      <!-- RIGHT SIDEBAR INSPECTOR -->
      <aside id="inspector-panel" class="w-96 border-l border-slate-800 bg-slate-900/95 backdrop-blur p-5 overflow-y-auto custom-scrollbar flex flex-col gap-4 shrink-0 transition-all">
        <div class="flex items-center justify-between pb-3 border-b border-slate-800">
          <div class="text-xs uppercase tracking-wider font-semibold text-slate-400 flex items-center gap-1.5">
            <svg class="w-4 h-4 text-cyan-400" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path></svg>
            Module Inspector
          </div>
          <span id="inspect-layer-tag" class="text-[10px] font-mono px-2 py-0.5 rounded-full bg-slate-800 text-slate-300">Select a file</span>
        </div>

        <div id="inspector-content" class="flex flex-col gap-4 text-xs">
          <!-- DEFAULT EMPTY STATE -->
          <div class="p-4 rounded-xl border border-dashed border-slate-800 text-center text-slate-500 flex flex-col items-center gap-2 my-10">
            <div class="text-2xl">👆</div>
            <div>Bấm vào một nốt bất kỳ trên đồ thị Trọng lực để xem phân tích chi tiết của module đó.</div>
          </div>
        </div>
      </aside>
    </section>

    <!-- ================= TAB 2: ARCHITECTURE LAYERS ================= -->
    <section id="view-layers" class="tab-content absolute inset-0 p-6 overflow-y-auto custom-scrollbar hidden bg-slate-950">
      <div class="max-w-6xl mx-auto flex flex-col gap-6">
        
        <!-- HEADER SUMMARY -->
        <div class="bg-gradient-to-r from-slate-900 via-slate-900 to-slate-950 border border-slate-800 rounded-2xl p-6 shadow-xl">
          <h2 class="text-lg font-bold text-slate-100 flex items-center gap-2">
            <span>Sơ đồ Kiến trúc Phân tầng Clean Architecture (BKIT STM32F407)</span>
          </h2>
          <p class="text-xs text-slate-400 mt-1 leading-relaxed">
            Hệ thống được thiết kế theo nguyên lý phân tầng: Luồng dữ liệu đi từ phần cứng ngoại vi (STM32/Sensors) $\to$ BSP Drivers $\to$ Core Services $\to$ Application FSM $\to$ Render UI & Lưu Flash.
          </p>
        </div>

        <!-- 4 TIERS ARCHITECTURE DIAGRAM -->
        <div class="flex flex-col gap-4">
          
          <!-- TIER 4: ENTRY POINT -->
          <div class="border border-red-900/60 bg-red-950/20 rounded-xl p-5 shadow-lg">
            <div class="flex items-center justify-between pb-3 mb-3 border-b border-red-900/40">
              <div class="flex items-center gap-2">
                <span class="px-2 py-0.5 rounded text-[10px] font-bold bg-red-600 text-white font-mono">LAYER 4</span>
                <h3 class="text-sm font-bold text-red-200">System Entry Point & RTOS Main Loop</h3>
              </div>
              <span class="text-xs font-mono text-red-400">src/main.c (82 LOC)</span>
            </div>
            <div class="grid grid-cols-1 md:grid-cols-3 gap-3 text-xs text-slate-300">
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-slate-200 mb-1">Static Context Allocation</div>
                <div class="text-slate-400 text-[11px]">Cấp phát tĩnh <code>g_app</code>, <code>g_channel_mgr</code>, <code>g_fault_mgr</code> trên BSS, không dùng Heap tránh phân mảnh RAM.</div>
              </div>
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-slate-200 mb-1">Thread Mode FSM Step</div>
                <div class="text-slate-400 text-[11px]">Vòng lặp <code>while(1)</code> chạy <code>app_run_step(&g_app, now)</code> kèm <code>k_msleep(10)</code> nhường CPU cho ISR.</div>
              </div>
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-slate-200 mb-1">Hardware Heartbeat</div>
                <div class="text-slate-400 text-[11px]">Toggle LED PE4 chu kỳ 500ms giám sát trực quan trạng thái CPU không bị treo.</div>
              </div>
            </div>
          </div>

          <!-- TIER 3: APPLICATION (FSM) -->
          <div class="border border-amber-900/60 bg-amber-950/20 rounded-xl p-5 shadow-lg">
            <div class="flex items-center justify-between pb-3 mb-3 border-b border-amber-900/40">
              <div class="flex items-center gap-2">
                <span class="px-2 py-0.5 rounded text-[10px] font-bold bg-amber-600 text-white font-mono">LAYER 3</span>
                <h3 class="text-sm font-bold text-amber-200">Application Presentation & State Machine (FSM)</h3>
              </div>
              <span class="text-xs font-mono text-amber-400">src/app/app_fsm.c & .h (1,646 LOC - 20.3%)</span>
            </div>
            <div class="grid grid-cols-1 md:grid-cols-4 gap-3 text-xs text-slate-300">
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-amber-300 mb-1">11 FSM States</div>
                <div class="text-slate-400 text-[11px]">Dashboard, Menu, Auth PIN, Edit Enum, Edit Numeric, Save Confirm, Alarm Popup, ScreenSaver.</div>
              </div>
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-amber-300 mb-1">Multi-tap Engine Link</div>
                <div class="text-slate-400 text-[11px]">Bắt sự kiện từ <code>keypad_multitap</code> để nhảy state và nhập chuỗi tên kênh / số thực.</div>
              </div>
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-amber-300 mb-1">AI Proposal Confirmation</div>
                <div class="text-slate-400 text-[11px]">Popup tự động hiện lên khi AI ESP32 gửi yêu cầu đổi mode qua UART6, xác nhận OK/Cancel vật lý.</div>
              </div>
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-amber-300 mb-1">Dirty-Flag LCD Render</div>
                <div class="text-slate-400 text-[11px]">Chỉ vẽ lại vùng thay đổi trên màn hình 8080 FSMC, chống giật lag và tối ưu tốc độ.</div>
              </div>
            </div>
          </div>

          <!-- TIER 2: SERVICES (MIDDLEWARE) -->
          <div class="border border-blue-900/60 bg-blue-950/20 rounded-xl p-5 shadow-lg">
            <div class="flex items-center justify-between pb-3 mb-3 border-b border-blue-900/40">
              <div class="flex items-center gap-2">
                <span class="px-2 py-0.5 rounded text-[10px] font-bold bg-blue-600 text-white font-mono">LAYER 2</span>
                <h3 class="text-sm font-bold text-blue-200">Core Services & Business Logic Engine</h3>
              </div>
              <span class="text-xs font-mono text-blue-400">src/services/ (3,478 LOC - 43.0%)</span>
            </div>
            <div class="grid grid-cols-1 md:grid-cols-4 gap-3 text-xs text-slate-300">
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-blue-300 mb-1">channel_manager (800 LOC)</div>
                <div class="text-slate-400 text-[11px]">Flash Persistence Sector 11 128KB (221 slots Rolling Slot bảo vệ tuổi thọ), chu kỳ lấy mẫu 18 kênh.</div>
              </div>
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-blue-300 mb-1">driver_registry & hw_caps</div>
                <div class="text-slate-400 text-[11px]">Bảng tra cứu con trỏ hàm <code>driver_ops_t</code> và ma trận khả năng phần cứng của 18 kênh vật lý.</div>
              </div>
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-blue-300 mb-1">fault_manager (244 LOC)</div>
                <div class="text-slate-400 text-[11px]">Lọc lỗi debounce, phân cấp độ nghiêm trọng INFO, WARN, ERROR, CRITICAL cho còi/alarm.</div>
              </div>
              <div class="bg-slate-900/80 p-3 rounded-lg border border-slate-800">
                <div class="font-semibold text-blue-300 mb-1">com_protocol (333 LOC)</div>
                <div class="text-slate-400 text-[11px]">Đóng gói telemetry uplink, giải mã khung lệnh cấu hình AI và kiểm tra CRC32 an toàn.</div>
              </div>
            </div>
          </div>

          <!-- TIER 1: BSP (DRIVERS) -->
          <div class="border border-emerald-900/60 bg-emerald-950/20 rounded-xl p-5 shadow-lg">
            <div class="flex items-center justify-between pb-3 mb-3 border-b border-emerald-900/40">
              <div class="flex items-center gap-2">
                <span class="px-2 py-0.5 rounded text-[10px] font-bold bg-emerald-600 text-white font-mono">LAYER 1</span>
                <h3 class="text-sm font-bold text-emerald-200">Board Support Package (Zephyr Drivers)</h3>
              </div>
              <span class="text-xs font-mono text-emerald-400">src/bsp/ (2,888 LOC - 35.7%)</span>
            </div>
            <div class="grid grid-cols-2 md:grid-cols-4 gap-3 text-xs text-slate-300">
              <div class="bg-slate-900/80 p-2.5 rounded-lg border border-slate-800">
                <div class="font-semibold text-emerald-300">bsp_lcd (1,107 LOC)</div>
                <div class="text-slate-500 text-[10px]">FSMC 8080 16-bit, Font bitmap 8x16, 12x24</div>
              </div>
              <div class="bg-slate-900/80 p-2.5 rounded-lg border border-slate-800">
                <div class="font-semibold text-emerald-300">bsp_adc (161 LOC)</div>
                <div class="text-slate-500 text-[10px]">STM32 ADC1, 4-20mA shunt, đứt dây</div>
              </div>
              <div class="bg-slate-900/80 p-2.5 rounded-lg border border-slate-800">
                <div class="font-semibold text-emerald-300">bsp_rs485 (325 LOC)</div>
                <div class="text-slate-500 text-[10px]">Modbus Master, UART3, điều khiển RE/DE</div>
              </div>
              <div class="bg-slate-900/80 p-2.5 rounded-lg border border-slate-800">
                <div class="font-semibold text-emerald-300">bsp_button (109 LOC)</div>
                <div class="text-slate-500 text-[10px]">74HC165 SPI Shift Register, 16 phím</div>
              </div>
              <div class="bg-slate-900/80 p-2.5 rounded-lg border border-slate-800">
                <div class="font-semibold text-emerald-300">bsp_din (113 LOC)</div>
                <div class="text-slate-500 text-[10px]">Đọc ngõ vào số & đếm xung GPIO</div>
              </div>
              <div class="bg-slate-900/80 p-2.5 rounded-lg border border-slate-800">
                <div class="font-semibold text-emerald-300">bsp_dout (129 LOC)</div>
                <div class="text-slate-500 text-[10px]">Đóng ngắt Relay & kiểm tra phản hồi</div>
              </div>
              <div class="bg-slate-900/80 p-2.5 rounded-lg border border-slate-800">
                <div class="font-semibold text-emerald-300">bsp_uart (69 LOC)</div>
                <div class="text-slate-500 text-[10px]">USART6 Zephyr k_msgq RX FIFO</div>
              </div>
              <div class="bg-slate-900/80 p-2.5 rounded-lg border border-slate-800">
                <div class="font-semibold text-emerald-300">lcd_font (409 LOC)</div>
                <div class="text-slate-500 text-[10px]">Mảng Bitmap ASCII characters</div>
              </div>
            </div>
          </div>

          <!-- TIER 0: HARDWARE -->
          <div class="border border-slate-800 bg-slate-900/40 rounded-xl p-4 flex items-center justify-between text-xs text-slate-400">
            <span class="font-mono font-bold text-slate-300">LAYER 0: STM32F407ZGT6 & Physical Interfaces</span>
            <span>FSMC (TFT LCD) | ADC1 | USART1 (Debug RTT) | USART6 (ESP32) | USART3 (RS485) | SPI2 (74HC165) | Flash Sector 11 (128KB)</span>
          </div>

        </div>
      </div>
    </section>

    <!-- ================= TAB 3: FSM STATE MACHINE ================= -->
    <section id="view-fsm" class="tab-content absolute inset-0 p-6 overflow-y-auto custom-scrollbar hidden bg-slate-950">
      <div class="max-w-6xl mx-auto flex flex-col gap-6">
        
        <div class="bg-slate-900 border border-slate-800 rounded-2xl p-6 shadow-xl flex items-center justify-between">
          <div>
            <h2 class="text-lg font-bold text-slate-100 flex items-center gap-2">
              <span>Bản đồ Chuyển dịch Trạng thái (Finite State Machine - 11 States)</span>
            </h2>
            <p class="text-xs text-slate-400 mt-1">
              File: <code class="text-amber-400">src/app/app_fsm.c</code> (1,476 LOC). Điều phối toàn bộ vòng đời ứng dụng, bảo vệ an toàn và tương tác bàn phím ITU E.161.
            </p>
          </div>
          <div class="flex items-center gap-2 text-xs font-mono">
            <span class="px-3 py-1 bg-amber-950/60 text-amber-400 border border-amber-800/80 rounded-lg">Inactivity: 60s</span>
            <span class="px-3 py-1 bg-cyan-950/60 text-cyan-400 border border-cyan-800/80 rounded-lg">Screensaver: 5m</span>
          </div>
        </div>

        <!-- 11 STATES GRID -->
        <div class="grid grid-cols-1 md:grid-cols-3 gap-4 text-xs">
          
          <div class="border border-slate-800 bg-slate-900/90 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-cyan-400 font-mono">0. FSM_STATE_INIT</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 text-slate-400">Boot</span>
            </div>
            <p class="text-slate-300 text-[11px]">Khởi động POST kiểm tra Flash, RTC, SD Card. Hiển thị Logo và phiên bản firmware trong 2-3s trước khi vào Dashboard.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">Next: FSM_HOME_DASHBOARD</div>
          </div>

          <div class="border border-emerald-900/60 bg-emerald-950/20 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-emerald-400 font-mono">1. HOME_DASHBOARD</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-emerald-950 text-emerald-400">Main State</span>
            </div>
            <p class="text-slate-300 text-[11px]">Màn hình trung tâm: Hiển thị giá trị 18 kênh đo thời gian thực, trạng thái REC, sóng Wi-Fi/4G và RTC. Phím UP/DOWN cuộn kênh, phím E vào Login.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">Trigger: [ENTER] -> AUTH_LOGIN</div>
          </div>

          <div class="border border-slate-800 bg-slate-900/90 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-amber-400 font-mono">2. AUTH_LOGIN</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 text-slate-400">Security</span>
            </div>
            <p class="text-slate-300 text-[11px]">Xác thực mã PIN (mặc định 4-6 số) trước khi cho phép vào Menu cấu hình phần cứng. Sai quá 3 lần sẽ lockout tạm thời.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">Success -> MAIN_MENU | Timeout -> HOME</div>
          </div>

          <div class="border border-slate-800 bg-slate-900/90 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-blue-400 font-mono">3. MAIN_MENU</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 text-slate-400">Nav</span>
            </div>
            <p class="text-slate-300 text-[11px]">Danh mục chính: 1. Cấu hình Kênh (Channels), 2. Ngưỡng cảnh báo (Alarms), 3. Kết nối Cloud (Uplink), 4. Hệ thống & Giờ thực (RTC), 5. Chẩn đoán.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">Next: SUBMENU_LIST</div>
          </div>

          <div class="border border-slate-800 bg-slate-900/90 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-blue-400 font-mono">4. SUBMENU_LIST</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 text-slate-400">Nav</span>
            </div>
            <p class="text-slate-300 text-[11px]">Danh sách chi tiết 18 kênh hoặc tham số cấu hình. Hỗ trợ cơ chế cửa sổ trượt (Sliding Viewport 6 dòng) chống tràn màn hình LCD.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">Next: EDIT_ENUM / EDIT_NUMERIC</div>
          </div>

          <div class="border border-slate-800 bg-slate-900/90 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-purple-400 font-mono">5. EDIT_ENUM</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 text-slate-400">Editor</span>
            </div>
            <p class="text-slate-300 text-[11px]">Chọn danh sách tùy chọn liệt kê: Loại cảm biến, Chế độ kênh (DISABLED, 4-20MA, 0-10V, DIN, DOUT, RS485). Bấm UP/DOWN xoay vòng.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">Next: SAVE_CONFIRM</div>
          </div>

          <div class="border border-slate-800 bg-slate-900/90 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-purple-400 font-mono">6. EDIT_NUMERIC</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 text-slate-400">Editor</span>
            </div>
            <p class="text-slate-300 text-[11px]">Nhập số thực qua bàn phím: Hệ số Calib (Slope, Offset), Ngưỡng Alarm High/Low. Hỗ trợ phím [*] cho dấu chấm thập phân, [#] cho số âm.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">Next: SAVE_CONFIRM</div>
          </div>

          <div class="border border-amber-900/60 bg-amber-950/20 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-amber-400 font-mono">7. SAVE_CONFIRM</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-amber-950 text-amber-400">Persistence</span>
            </div>
            <p class="text-slate-300 text-[11px]">Xác nhận lưu thay đổi: ENTER để commit vào Flash Sector 11 (Rolling Slot), BACK để rollback RAM từ Flash bản cũ để chống Desync Bug.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">Commit Flash -> SUBMENU</div>
          </div>

          <div class="border border-rose-900/60 bg-rose-950/20 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-rose-400 font-mono">8. ALARM_POPUP</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-rose-950 text-rose-400">Safety Critical</span>
            </div>
            <p class="text-slate-300 text-[11px]">Cửa sổ cảnh báo khẩn cấp: Đứt dây tín hiệu 4-20mA, vượt ngưỡng đo, lỗi giao tiếp RS485. Kêu còi Buzzer, yêu cầu bấm nút ACK để tắt còi.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">ACK -> Previous State</div>
          </div>

          <div class="border border-slate-800 bg-slate-900/90 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-indigo-400 font-mono">9. DIAGNOSTICS_LIVE</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 text-slate-400">Hardware Test</span>
            </div>
            <p class="text-slate-300 text-[11px]">Màn hình kỹ sư: Xem trực tiếp giá trị điện áp thô ADC mV, chuỗi Hex thanh ghi Modbus RS485 và tần số xung DIN để đối soát phần cứng.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">BACK -> MAIN_MENU</div>
          </div>

          <div class="border border-slate-800 bg-slate-900/90 rounded-xl p-4 flex flex-col gap-2">
            <div class="flex items-center justify-between">
              <span class="font-bold text-slate-400 font-mono">10. SCREENSAVER_SLEEP</span>
              <span class="text-[10px] px-1.5 py-0.5 rounded bg-slate-800 text-slate-400">Power Save</span>
            </div>
            <p class="text-slate-300 text-[11px]">Tự động kích hoạt sau 5 phút không có thao tác phím. Tắt đèn nền LCD (Backlight PWM 0%) để bảo vệ tuổi thọ màn hình. Bấm phím bất kỳ để đánh thức.</p>
            <div class="mt-auto pt-2 border-t border-slate-800/60 text-[10px] text-slate-500 font-mono">Any Key -> HOME_DASHBOARD</div>
          </div>

        </div>
      </div>
    </section>

    <!-- ================= TAB 4: CODE AUDIT & INVERSIONS ================= -->
    <section id="view-audit" class="tab-content absolute inset-0 p-6 overflow-y-auto custom-scrollbar hidden bg-slate-950">
      <div class="max-w-6xl mx-auto flex flex-col gap-6">
        
        <div class="bg-slate-900 border border-slate-800 rounded-2xl p-6 shadow-xl">
          <h2 class="text-lg font-bold text-rose-300 flex items-center gap-2">
            <span>Báo cáo Rà soát Codebase & Nợ Kiến trúc (Code Review & Architectural Debt)</span>
          </h2>
          <p class="text-xs text-slate-400 mt-1">
            Tổng hợp các điểm nghẽn, vi phạm nguyên lý Clean Architecture và các nguy cơ lỗi ngầm cần khắc phục trong dự án.
          </p>
        </div>

        <!-- 3 MAJOR ISSUES CARDS -->
        <div class="grid grid-cols-1 md:grid-cols-3 gap-5">
          
          <div class="border border-rose-900/70 bg-rose-950/20 rounded-xl p-5 flex flex-col gap-3">
            <div class="flex items-center gap-2 text-rose-300 font-bold text-sm">
              <span class="text-base">⚠️</span>
              <span>1. Inverted Dependency (BSP $\to$ Services)</span>
            </div>
            <p class="text-xs text-slate-300 leading-relaxed">
              Các file trong <code>src/bsp/</code> (tầng Driver phần cứng) đang trực tiếp <code>#include "channel_manager.h"</code> và <code>"fault_manager.h"</code> (tầng Middleware Service).
            </p>
            <div class="bg-slate-950/80 p-3 rounded-lg border border-slate-800 text-[11px] font-mono text-slate-400">
              <div class="text-rose-400 font-bold mb-1">Hậu quả & Rủi ro:</div>
              - Gây phụ thuộc vòng (Circular Dependency).<br>
              - Driver BSP mất tính độc lập, không thể tái sử dụng cho project khác.<br>
              - Rebuild 1 thay đổi nhỏ ở Service làm compile lại toàn bộ BSP.
            </div>
            <div class="mt-auto text-[11px] text-emerald-400 bg-emerald-950/40 border border-emerald-900/60 p-2.5 rounded-lg">
              <b>Khuyến nghị tối ưu:</b> Tách struct <code>ChannelConfig_t</code> ra file header độc lập và truyền hàm callback / struct interface thay vì include trực tiếp.
            </div>
          </div>

          <div class="border border-amber-900/70 bg-amber-950/20 rounded-xl p-5 flex flex-col gap-3">
            <div class="flex items-center gap-2 text-amber-300 font-bold text-sm">
              <span class="text-base">🐘</span>
              <span>2. God Module Antipattern (app_fsm.c)</span>
            </div>
            <p class="text-xs text-slate-300 leading-relaxed">
              <code>src/app/app_fsm.c</code> hiện dài tới <b>1,476 LOC</b>, đang ôm đồm cùng lúc 4 trách nhiệm khác nhau:
            </p>
            <div class="bg-slate-950/80 p-3 rounded-lg border border-slate-800 text-[11px] font-mono text-slate-400">
              1. Logic chuyển dịch trạng thái (FSM core)<br>
              2. Logic gõ phím & bộ đệm text buffer<br>
              3. Vẽ chi tiết đồ họa từng pixel lên LCD<br>
              4. Xử lý timeout và quản lý bộ nhớ tạm
            </div>
            <div class="mt-auto text-[11px] text-emerald-400 bg-emerald-950/40 border border-emerald-900/60 p-2.5 rounded-lg">
              <b>Khuyến nghị tối ưu:</b> Tách thành các view renderer riêng: <code>view_dashboard.c</code>, <code>view_menu.c</code>, <code>view_editor.c</code>; <code>app_fsm.c</code> chỉ giữ vai trò Router điều phối.
            </div>
          </div>

          <div class="border border-cyan-900/70 bg-cyan-950/20 rounded-xl p-5 flex flex-col gap-3">
            <div class="flex items-center gap-2 text-cyan-300 font-bold text-sm">
              <span class="text-base">⏳</span>
              <span>3. Single-Threaded Blocking Risk</span>
            </div>
            <p class="text-xs text-slate-300 leading-relaxed">
              Trong <code>main.c</code>, FSM step, đọc bàn phím và render LCD đều chạy trên cùng một luồng duy nhất (Thread Mode) với <code>k_msleep(10)</code>.
            </p>
            <div class="bg-slate-950/80 p-3 rounded-lg border border-slate-800 text-[11px] font-mono text-slate-400">
              <div class="text-cyan-400 font-bold mb-1">Hậu quả & Rủi ro:</div>
              - Khi vẽ màn hình phức tạp (FSMC tốn hàng chục ms), việc đọc phím và xử lý UART có thể bị giật hoặc drop gói tin nếu FIFO quá nhỏ.
            </div>
            <div class="mt-auto text-[11px] text-emerald-400 bg-emerald-950/40 border border-emerald-900/60 p-2.5 rounded-lg">
              <b>Khuyến nghị tối ưu:</b> Tận dụng Zephyr RTOS đa luồng: Tách <b>UI Thread</b> (Low Priority) và <b>Sampling/Com Thread</b> (High Priority) giao tiếp qua <code>k_msgq</code>.
            </div>
          </div>

        </div>

        <!-- TABLE OF VIOLATIONS -->
        <div class="border border-slate-800 bg-slate-900/80 rounded-xl p-5">
          <h3 class="text-sm font-bold text-slate-200 mb-3 flex items-center gap-2">
            <span>Danh sách 9 điểm vi phạm Đảo ngược Phụ thuộc cụ thể trong code:</span>
          </h3>
          <div class="overflow-x-auto">
            <table class="w-full text-left text-xs font-mono">
              <thead class="text-slate-400 border-b border-slate-800 text-[11px]">
                <tr>
                  <th class="py-2 px-3">#</th>
                  <th class="py-2 px-3">File nguồn (Lower)</th>
                  <th class="py-2 px-3">Tầng nguồn</th>
                  <th class="py-2 px-3">Include (Higher)</th>
                  <th class="py-2 px-3">Tầng đích</th>
                  <th class="py-2 px-3">Lý do & Cách sửa</th>
                </tr>
              </thead>
              <tbody class="divide-y divide-slate-800/60 text-slate-300" id="violation-table-body">
                <!-- Dynamically populated -->
              </tbody>
            </table>
          </div>
        </div>

      </div>
    </section>

  </main>

  <script>
    // Embedded JSON metadata
    const GRAPH_DATA = {json_data};

    // State
    let currentTab = 'gravity';
    let simulationRunning = true;
    let selectedNode = null;
    let hoveredNode = null;
    let searchQuery = '';
    let activeLayerFilter = 'All';

    // Canvas & Viewport Transform
    const canvas = document.getElementById('gravity-canvas');
    const ctx = canvas.getContext('2d');
    const container = document.getElementById('canvas-container');

    let transform = {{ x: 0, y: 0, k: 1 }};
    let isPanning = false;
    let panStart = {{ x: 0, y: 0 }};
    let draggedNode = null;

    // Build Physics Node Map
    const nodeMap = {{}};
    const nodes = GRAPH_DATA.nodes.map(n => {{
      const radius = Math.max(12, Math.min(36, Math.sqrt(n.loc) * 0.95));
      const obj = {{
        ...n,
        radius,
        x: (Math.random() - 0.5) * 600,
        y: (Math.random() - 0.5) * 500,
        vx: 0,
        vy: 0,
        pinned: false
      }};
      nodeMap[n.id] = obj;
      return obj;
    }});

    // Build Edge Array
    const edges = GRAPH_DATA.edges.map(e => ({{
      source: nodeMap[e.source],
      target: nodeMap[e.target],
      is_violation: e.is_violation
    }})).filter(e => e.source && e.target);

    // Initial layer-based positioning
    function initNodePositions() {{
      const W = canvas.width || 1200;
      const H = canvas.height || 800;
      nodes.forEach(node => {{
        let targetY = 0;
        if (node.layer === 'Core / Entry') targetY = -H * 0.35;
        else if (node.layer === 'Application (FSM)') targetY = -H * 0.18;
        else if (node.layer === 'Services (Middleware)') targetY = H * 0.05;
        else if (node.layer === 'BSP (Drivers)') targetY = H * 0.25;
        else targetY = H * 0.40; // Core / Contracts

        node.y = targetY + (Math.random() - 0.5) * 80;
        node.x = (Math.random() - 0.5) * (W * 0.7);
      }});
      transform.x = W / 2;
      transform.y = H / 2;
      transform.k = 0.9;
    }}

    function resizeCanvas() {{
      canvas.width = container.clientWidth;
      canvas.height = container.clientHeight;
      if (transform.x === 0 && transform.y === 0) {{
        initNodePositions();
      }}
    }}
    window.addEventListener('resize', resizeCanvas);
    resizeCanvas();

    // ================= PHYSICS SIMULATION =================
    function tickPhysics() {{
      if (!simulationRunning) return;

      const repulsionK = 2200;
      const springK = 0.045;
      const restLength = 110;
      const damping = 0.82;
      const centerGravity = 0.008;

      const H = canvas.height || 800;

      // 1. Repulsion between all visible node pairs
      const activeNodes = nodes.filter(n => isNodeVisible(n));
      for (let i = 0; i < activeNodes.length; i++) {{
        const n1 = activeNodes[i];
        for (let j = i + 1; j < activeNodes.length; j++) {{
          const n2 = activeNodes[j];
          const dx = n2.x - n1.x;
          const dy = n2.y - n1.y;
          const distSq = dx * dx + dy * dy || 1;
          const dist = Math.sqrt(distSq);

          if (dist < 400) {{
            const f = repulsionK / (distSq + 200);
            const fx = (dx / dist) * f;
            const fy = (dy / dist) * f;

            if (!n1.pinned) {{ n1.vx -= fx; n1.vy -= fy; }}
            if (!n2.pinned) {{ n2.vx += fx; n2.vy += fy; }}
          }}
        }}
      }}

      // 2. Spring tension along edges
      edges.forEach(e => {{
        if (!isNodeVisible(e.source) || !isNodeVisible(e.target)) return;
        const dx = e.target.x - e.source.x;
        const dy = e.target.y - e.source.y;
        const dist = Math.sqrt(dx * dx + dy * dy) || 1;
        const disp = dist - restLength;
        const f = springK * disp;

        const fx = (dx / dist) * f;
        const fy = (dy / dist) * f;

        if (!e.source.pinned) {{ e.source.vx += fx; e.source.vy += fy; }}
        if (!e.target.pinned) {{ e.target.vx -= fx; e.target.vy -= fy; }}
      }});

      // 3. Layer Band Gravity & Damping
      activeNodes.forEach(node => {{
        if (node.pinned) return;

        let targetY = 0;
        if (node.layer === 'Core / Entry') targetY = -240;
        else if (node.layer === 'Application (FSM)') targetY = -110;
        else if (node.layer === 'Services (Middleware)') targetY = 30;
        else if (node.layer === 'BSP (Drivers)') targetY = 170;
        else targetY = 290; // Core / Contracts

        // Gravity to layer band
        node.vy += (targetY - node.y) * 0.035;
        // Gravity to X center
        node.vx += (-node.x) * centerGravity;

        // Apply velocity & damping
        node.vx *= damping;
        node.vy *= damping;

        node.x += node.vx;
        node.y += node.vy;
      }});
    }}

    function isNodeVisible(node) {{
      if (activeLayerFilter !== 'All' && node.layer !== activeLayerFilter) return false;
      if (searchQuery) {{
        const q = searchQuery.toLowerCase();
        const match = node.id.toLowerCase().includes(q) ||
                      node.path.toLowerCase().includes(q) ||
                      node.funcs.some(f => f.toLowerCase().includes(q));
        return match;
      }}
      return true;
    }}

    // ================= RENDER LOOP =================
    function render() {{
      tickPhysics();

      ctx.clearRect(0, 0, canvas.width, canvas.height);
      ctx.save();
      ctx.translate(transform.x, transform.y);
      ctx.scale(transform.k, transform.k);

      // Draw subtle horizontal layer background guide lines
      ctx.strokeStyle = 'rgba(51, 65, 85, 0.25)';
      ctx.lineWidth = 1;
      ctx.setLineDash([4, 6]);
      [-240, -110, 50, 220].forEach((y, idx) => {{
        ctx.beginPath();
        ctx.moveTo(-1500, y);
        ctx.lineTo(1500, y);
        ctx.stroke();
      }});
      ctx.setLineDash([]);

      // Draw Layer Labels on canvas background
      ctx.font = '11px monospace';
      ctx.fillStyle = 'rgba(148, 163, 184, 0.3)';
      ctx.textAlign = 'left';
      ctx.fillText('LAYER 4: CORE ENTRY', -800, -250);
      ctx.fillText('LAYER 3: APPLICATION (FSM)', -800, -120);
      ctx.fillText('LAYER 2: SERVICES (MIDDLEWARE)', -800, 40);
      ctx.fillText('LAYER 1: BSP DRIVERS', -800, 210);

      // Render Edges
      edges.forEach(e => {{
        if (!isNodeVisible(e.source) || !isNodeVisible(e.target)) return;

        const isHighlighted = selectedNode && (e.source === selectedNode || e.target === selectedNode);
        const isDimmed = selectedNode && !isHighlighted;

        ctx.beginPath();
        ctx.moveTo(e.source.x, e.source.y);
        ctx.lineTo(e.target.x, e.target.y);

        if (e.is_violation) {{
          // Violation: Bright pulsing red
          ctx.strokeStyle = isDimmed ? 'rgba(239, 68, 68, 0.15)' : 'rgba(239, 68, 68, 0.9)';
          ctx.lineWidth = isHighlighted ? 3 : 2;
          ctx.setLineDash([5, 4]);
        }} else {{
          // Normal: Subtle slate/cyan
          if (isHighlighted) {{
            ctx.strokeStyle = '#38bdf8';
            ctx.lineWidth = 2.5;
            ctx.setLineDash([]);
          }} else {{
            ctx.strokeStyle = isDimmed ? 'rgba(71, 85, 105, 0.1)' : 'rgba(100, 116, 139, 0.35)';
            ctx.lineWidth = 1;
            ctx.setLineDash([]);
          }}
        }}
        ctx.stroke();
        ctx.setLineDash([]);
      }});

      // Render Nodes
      nodes.forEach(node => {{
        if (!isNodeVisible(node)) return;

        const isSelected = selectedNode === node;
        const isHovered = hoveredNode === node;
        const isNeighbor = selectedNode && (selectedNode.out_edges.includes(node.id) || selectedNode.in_edges.includes(node.id));
        const isDimmed = selectedNode && !isSelected && !isNeighbor;

        const alpha = isDimmed ? 0.2 : 1.0;

        // Node Glow Ring if selected or hovered
        if (isSelected || isHovered) {{
          ctx.beginPath();
          ctx.arc(node.x, node.y, node.radius + 7, 0, Math.PI * 2);
          ctx.fillStyle = isSelected ? 'rgba(56, 189, 248, 0.25)' : 'rgba(255, 255, 255, 0.15)';
          ctx.fill();
        }}

        // Node Body
        ctx.beginPath();
        ctx.arc(node.x, node.y, node.radius, 0, Math.PI * 2);
        ctx.fillStyle = node.color;
        ctx.globalAlpha = alpha;
        ctx.fill();

        // Node Border
        ctx.strokeStyle = isSelected ? '#ffffff' : 'rgba(255, 255, 255, 0.4)';
        ctx.lineWidth = isSelected ? 2.5 : 1.5;
        ctx.stroke();
        ctx.globalAlpha = 1.0;

        // Node Label
        ctx.font = isSelected ? 'bold 12px sans-serif' : '10px sans-serif';
        ctx.fillStyle = isDimmed ? 'rgba(148, 163, 184, 0.3)' : '#f8fafc';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'top';
        ctx.fillText(node.id, node.x, node.y + node.radius + 4);
      }});

      ctx.restore();
      requestAnimationFrame(render);
    }}
    requestAnimationFrame(render);

    // ================= USER INTERACTIONS (MOUSE / ZOOM) =================
    function getMousePosOnCanvas(e) {{
      const rect = canvas.getBoundingClientRect();
      const clientX = e.clientX;
      const clientY = e.clientY;
      const screenX = clientX - rect.left;
      const screenY = clientY - rect.top;
      return {{
        x: (screenX - transform.x) / transform.k,
        y: (screenY - transform.y) / transform.k,
        screenX,
        screenY
      }};
    }}

    function findNodeAt(x, y) {{
      for (let i = nodes.length - 1; i >= 0; i--) {{
        const n = nodes[i];
        if (!isNodeVisible(n)) continue;
        const dx = x - n.x;
        const dy = y - n.y;
        if (dx * dx + dy * dy <= (n.radius + 4) * (n.radius + 4)) {{
          return n;
        }}
      }}
      return null;
    }}

    canvas.addEventListener('mousedown', e => {{
      const pos = getMousePosOnCanvas(e);
      const hit = findNodeAt(pos.x, pos.y);
      if (hit) {{
        draggedNode = hit;
        hit.pinned = true;
        selectNode(hit);
      }} else {{
        isPanning = true;
        panStart.x = e.clientX - transform.x;
        panStart.y = e.clientY - transform.y;
      }}
    }});

    window.addEventListener('mousemove', e => {{
      const pos = getMousePosOnCanvas(e);
      hoveredNode = findNodeAt(pos.x, pos.y);

      if (draggedNode) {{
        draggedNode.x = pos.x;
        draggedNode.y = pos.y;
        draggedNode.vx = 0;
        draggedNode.vy = 0;
      }} else if (isPanning) {{
        transform.x = e.clientX - panStart.x;
        transform.y = e.clientY - panStart.y;
      }}
    }});

    window.addEventListener('mouseup', () => {{
      if (draggedNode) {{
        draggedNode.pinned = false;
        draggedNode = null;
      }}
      isPanning = false;
    }});

    canvas.addEventListener('wheel', e => {{
      e.preventDefault();
      const pos = getMousePosOnCanvas(e);
      const zoomFactor = e.deltaY < 0 ? 1.12 : 0.89;
      const newK = Math.max(0.2, Math.min(3.5, transform.k * zoomFactor));

      transform.x = pos.screenX - pos.x * newK;
      transform.y = pos.screenY - pos.y * newK;
      transform.k = newK;
    }}, {{ passive: false }});

    // ================= INSPECTOR & SELECTION =================
    function selectNode(node) {{
      selectedNode = node;
      const panel = document.getElementById('inspector-content');
      const tag = document.getElementById('inspect-layer-tag');

      tag.textContent = node.layer;
      tag.style.color = node.color;

      const incsHtml = node.out_edges.length > 0 
        ? node.out_edges.map(inc => `<span class="px-2 py-0.5 rounded bg-slate-800 text-slate-300 font-mono text-[10px] cursor-pointer hover:bg-slate-700" onclick="focusOnNode('${{inc}}')">${{inc}}</span>`).join('')
        : '<span class="text-slate-500 italic">None</span>';

      const depsHtml = node.in_edges.length > 0
        ? node.in_edges.map(dep => `<span class="px-2 py-0.5 rounded bg-slate-800 text-slate-300 font-mono text-[10px] cursor-pointer hover:bg-slate-700" onclick="focusOnNode('${{dep}}')">${{dep}}</span>`).join('')
        : '<span class="text-slate-500 italic">None (Leaf)</span>';

      const funcsHtml = node.funcs.length > 0
        ? node.funcs.map(f => `<div class="font-mono text-[11px] text-cyan-400/90 py-0.5 truncate">• ${{f}}()</div>`).join('')
        : '<div class="text-slate-500 italic">Header file / Data definitions</div>';

      // Check if node has violations
      const nodeViolations = GRAPH_DATA.violations.filter(v => v.from === node.id);
      let violationNotice = '';
      if (nodeViolations.length > 0) {{
        violationNotice = `
          <div class="p-3 rounded-xl bg-rose-950/40 border border-rose-900/80 text-rose-300 text-xs flex flex-col gap-1.5">
            <div class="font-bold flex items-center gap-1.5 text-rose-400">
              <span>⚠️ Inverted Dependency Warning:</span>
            </div>
            <div class="text-[11px] text-slate-300">
              Module thuộc tầng <b>${{node.layer}}</b> nhưng lại include trực tiếp:
              <ul class="list-disc pl-4 text-rose-300 font-mono text-[10px] mt-1">
                ${{nodeViolations.map(v => `<li>${{v.to}} (${{v.to_layer}})</li>`).join('')}}
              </ul>
            </div>
          </div>
        `;
      }}

      panel.innerHTML = `
        <div class="flex items-start justify-between">
          <div>
            <h3 class="text-base font-bold text-slate-100 font-mono">${{node.id}}</h3>
            <div class="text-slate-500 text-[11px] font-mono">${{node.path}}</div>
          </div>
          <div class="text-right">
            <div class="text-xs font-bold text-emerald-400 font-mono">${{node.loc}} LOC</div>
            <div class="text-[10px] text-slate-400">${{node.func_count}} functions</div>
          </div>
        </div>

        <div class="p-3 rounded-xl bg-slate-950/80 border border-slate-800 text-xs text-slate-300 leading-relaxed">
          <div class="font-semibold text-slate-200 mb-1">Kiến trúc & Trách nhiệm:</div>
          ${{node.role}}
        </div>

        ${{violationNotice}}

        <div>
          <div class="text-slate-400 text-[11px] font-semibold mb-1.5">File này phụ thuộc vào (Outgoing Includes):</div>
          <div class="flex flex-wrap gap-1.5">${{incsHtml}}</div>
        </div>

        <div>
          <div class="text-slate-400 text-[11px] font-semibold mb-1.5">Các file phụ thuộc vào file này (Incoming Dependents):</div>
          <div class="flex flex-wrap gap-1.5">${{depsHtml}}</div>
        </div>

        <div>
          <div class="text-slate-400 text-[11px] font-semibold mb-1.5">Hàm định nghĩa / xử lý chính:</div>
          <div class="bg-slate-950/80 p-2.5 rounded-xl border border-slate-800 max-h-48 overflow-y-auto custom-scrollbar">
            ${{funcsHtml}}
          </div>
        </div>

        <button onclick="clearSelection()" class="w-full py-1.5 mt-2 rounded-lg bg-slate-800 hover:bg-slate-700 text-slate-300 text-xs font-mono transition">
          ✕ Bỏ chọn (Xem toàn bộ)
        </button>
      `;
    }}

    function focusOnNode(id) {{
      const n = nodeMap[id];
      if (n) {{
        selectNode(n);
        transform.x = canvas.width / 2 - n.x * transform.k;
        transform.y = canvas.height / 2 - n.y * transform.k;
      }}
    }}

    function clearSelection() {{
      selectedNode = null;
      document.getElementById('inspect-layer-tag').textContent = 'Select a file';
      document.getElementById('inspect-layer-tag').style.color = '#94a3b8';
      document.getElementById('inspector-content').innerHTML = `
        <div class="p-4 rounded-xl border border-dashed border-slate-800 text-center text-slate-500 flex flex-col items-center gap-2 my-10">
          <div class="text-2xl">👆</div>
          <div>Bấm vào một nốt bất kỳ trên đồ thị Trọng lực để xem phân tích chi tiết của module đó.</div>
        </div>
      `;
    }}

    // ================= CONTROLS & FILTERS =================
    function toggleSimulation() {{
      simulationRunning = !simulationRunning;
      document.getElementById('btn-pause-sim').textContent = simulationRunning ? '⏸ Freeze' : '▶ Resume';
    }}

    function resetPhysicsLayout() {{
      initNodePositions();
    }}

    function zoomBy(factor) {{
      transform.k = Math.max(0.2, Math.min(3.5, transform.k * factor));
    }}

    function resetZoom() {{
      transform.k = 1.0;
      transform.x = canvas.width / 2;
      transform.y = canvas.height / 2;
    }}

    function toggleLayerFilter(layer) {{
      activeLayerFilter = layer;
      document.querySelectorAll('.filter-layer-btn').forEach(btn => {{
        btn.classList.remove('ring-2', 'ring-cyan-500', 'bg-slate-700');
      }});
      const targetBtn = document.getElementById(
        layer === 'All' ? 'btn-filter-All' :
        layer === 'Core / Entry' ? 'btn-filter-Core' :
        layer === 'Application (FSM)' ? 'btn-filter-App' :
        layer === 'Services (Middleware)' ? 'btn-filter-Services' :
        layer === 'BSP (Drivers)' ? 'btn-filter-BSP' : 'btn-filter-Contracts'
      );
      if (targetBtn) targetBtn.classList.add('ring-2', 'ring-cyan-500');
    }}

    const searchInput = document.getElementById('node-search');
    searchInput.addEventListener('input', e => {{
      searchQuery = e.target.value.trim();
    }});

    function clearSearch() {{
      searchInput.value = '';
      searchQuery = '';
    }}

    // ================= TABS SWITCHER =================
    function switchTab(tabId) {{
      currentTab = tabId;
      document.querySelectorAll('.tab-btn').forEach(btn => {{
        btn.className = 'tab-btn px-3 py-1.5 rounded-md text-slate-400 hover:text-white transition-all';
      }});
      document.getElementById('tab-btn-' + tabId).className = 'tab-btn px-3 py-1.5 rounded-md bg-cyan-600 text-white font-semibold transition-all shadow';

      document.querySelectorAll('.tab-content').forEach(el => el.classList.add('hidden'));
      document.getElementById('view-' + tabId).classList.remove('hidden');

      if (tabId === 'gravity') {{
        resizeCanvas();
      }}
    }}

    // Populate Audit Table
    const auditTbody = document.getElementById('violation-table-body');
    if (GRAPH_DATA.violations.length === 0) {{
      auditTbody.innerHTML = '<tr><td colspan="6" class="py-4 text-center text-emerald-400">Không có vi phạm cấu trúc phân tầng.</td></tr>';
    }} else {{
      auditTbody.innerHTML = GRAPH_DATA.violations.map((v, i) => `
        <tr class="hover:bg-slate-800/40 transition">
          <td class="py-2.5 px-3 text-slate-500">${{i + 1}}</td>
          <td class="py-2.5 px-3 font-bold text-rose-400">${{v.from}}</td>
          <td class="py-2.5 px-3 text-slate-400">${{v.from_layer}}</td>
          <td class="py-2.5 px-3 font-bold text-cyan-400">${{v.to}}</td>
          <td class="py-2.5 px-3 text-slate-400">${{v.to_layer}}</td>
          <td class="py-2.5 px-3 text-slate-300 text-[11px]">${{v.reason}}</td>
        </tr>
      `).join('');
    }}
  </script>
</body>
</html>
"""
    return html

def main():
    result = analyze_codebase()
    
    # Write JSON
    out_json = WORKSPACE_DIR / "scripts" / "codebase_graph.json"
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, ensure_ascii=False)

    # Generate HTML
    html_content = generate_gravity_html(result)
    out_html = WORKSPACE_DIR / "scripts" / "gravity_view.html"
    with open(out_html, "w", encoding="utf-8") as f:
        f.write(html_content)

    print(f"[+] Codebase analyzed: {len(result['nodes'])} files, {result['total_loc']:,} LOC.")
    print(f"[+] HTML Gravity View generated at: {out_html}")

if __name__ == "__main__":
    main()
