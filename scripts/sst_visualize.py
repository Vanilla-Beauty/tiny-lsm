import struct
import os
import html
import sys

def read_uint16(data, offset):
    """从数据缓冲区中读取一个16位无符号整数。"""
    return struct.unpack_from('<H', data, offset)[0]

def read_uint32(data, offset):
    """从数据缓冲区中读取一个32位无符号整数。"""
    return struct.unpack_from('<I', data, offset)[0]

def read_uint64(data, offset):
    """从数据缓冲区中读取一个64位无符号整数。"""
    return struct.unpack_from('<Q', data, offset)[0]

def read_double(data, offset):
    """从数据缓冲区中读取一个双精度浮点数。"""
    return struct.unpack_from('<d', data, offset)[0]

def html_table(rows, headers, table_class='data-table'):
    """根据数据行和表头生成HTML表格。"""
    table_html = f"<table class='{table_class}'><thead><tr>"
    for h in headers:
        table_html += f"<th>{h}</th>"
    table_html += "</tr></thead><tbody>"
    for row in rows:
        table_html += "<tr>"
        for cell in row:
            safe_cell = html.escape(str(cell))
            table_html += f"<td>{safe_cell}</td>"
        table_html += "</tr>"
    table_html += "</tbody></table>"
    return table_html

def parse_bloom_filter(bloom_data):
    """
    解析Bloom Filter的二进制数据, 只生成元数据表格的HTML。
    布局: [expected_elements (u64)][fp_rate (double)][num_bits (u64)][num_hashes (u64)][bits...].
    """
    header_size = 8 + 8 + 8 + 8
    if not bloom_data or len(bloom_data) < header_size:
        return "<div class='hex-preview'>N/A (数据不足或不存在)</div>"

    try:
        # 解析元数据
        expected_elements = read_uint64(bloom_data, 0)
        fp_rate = read_double(bloom_data, 8)
        num_bits = read_uint64(bloom_data, 16)
        num_hashes = read_uint64(bloom_data, 24)

        metadata_rows = [
            ["预期元素数量 (expected_elements)", f"{expected_elements:,}"],
            ["假阳性率 (false_positive_rate)", f"{fp_rate:.10f}"],
            ["位图大小 (num_bits)", f"{num_bits:,}"],
            ["哈希函数数量 (num_hashes)", f"{num_hashes:,}"]
        ]
        # 直接返回元数据表格的HTML
        return html_table(metadata_rows, ["参数", "值"], table_class='data-table bloom-meta-table')

    except (struct.error, IndexError) as e:
        return f"<div class='hex-preview'>解析失败: {e}<br>{bloom_data.hex()}</div>"


def parse_sst(filename):
    if not os.path.exists(filename):
        print(f"错误: 文件 '{filename}' 不存在。")
        return

    with open(filename, 'rb') as f:
        data = f.read()
    
    file_size = len(data)

    extra_len = 4 * 2 + 8 * 2
    if file_size < extra_len:
        print("错误: 文件太小，不是一个有效的SST文件。")
        return
        
    extra_offset = file_size - extra_len
    meta_section_offset = read_uint32(data, extra_offset)
    bloom_offset = read_uint32(data, extra_offset + 4)
    min_tranc_id = read_uint64(data, extra_offset + 8)
    max_tranc_id = read_uint64(data, extra_offset + 16)

    section_points = [('blocks', 0), ('extra', extra_offset), ('file_end', file_size)]
    if meta_section_offset > 0:
        section_points.append(('meta', meta_section_offset))
    if bloom_offset > 0 and bloom_offset != meta_section_offset:
        section_points.append(('bloom', bloom_offset))

    unique_points = sorted(list(set(section_points)), key=lambda item: item[1])

    boundaries = {}
    for i in range(len(unique_points) - 1):
        current_name, current_start = unique_points[i]
        _, next_start = unique_points[i+1]
        boundaries[current_name] = {'start': current_start, 'end': next_start, 'size': next_start - current_start}

    def get_boundary(name, key, default=0):
        return boundaries.get(name, {}).get(key, default)

    block_section_end = get_boundary('blocks', 'end')
    block_section_size = get_boundary('blocks', 'size')
    meta_section_start = get_boundary('meta', 'start')
    meta_section_end = get_boundary('meta', 'end')
    meta_section_size = get_boundary('meta', 'size')
    bloom_section_start = get_boundary('bloom', 'start')
    bloom_section_end = get_boundary('bloom', 'end')
    bloom_section_size = get_boundary('bloom', 'size')
    extra_section_size = extra_len

    meta_rows = []
    num_meta_entries = 0
    if meta_section_size > 4:
        meta_data = data[meta_section_start:meta_section_end]
        num_meta_entries = read_uint32(meta_data, 0)
        offset = 4
        for i in range(num_meta_entries):
            try:
                block_offset = read_uint32(meta_data, offset)
                offset += 4
                first_key_len = read_uint16(meta_data, offset)
                offset += 2
                first_key = meta_data[offset:offset+first_key_len].decode('utf-8', errors='replace')
                offset += first_key_len
                last_key_len = read_uint16(meta_data, offset)
                offset += 2
                last_key = meta_data[offset:offset+last_key_len].decode('utf-8', errors='replace')
                offset += last_key_len
                meta_rows.append([i, block_offset, first_key, last_key])
            except (struct.error, IndexError):
                print(f"警告: 无法解析元数据条目 {i}。")
                break
    
    block_tables = []
    for i, meta in enumerate(meta_rows):
        current_block_offset = meta[1]
        next_block_offset = meta_rows[i+1][1] if i + 1 < len(meta_rows) else block_section_end
        block_data = data[current_block_offset:next_block_offset]
        
        entries = []
        if len(block_data) < 6:
            continue
            
        try:
            hash_size = 4
            num_elements_offset = len(block_data) - 2 - hash_size
            num_elements = struct.unpack_from('<H', block_data, num_elements_offset)[0]
            offsets_start = num_elements_offset - 2 * num_elements
            
            if offsets_start < 0:
                print(f"警告: Block {i} 中的元数据无效。")
                continue

            offsets = [struct.unpack_from('<H', block_data, offsets_start + 2*j)[0] for j in range(num_elements)]
            
            for idx, entry_offset in enumerate(offsets):
                off = entry_offset
                key_len = read_uint16(block_data, off)
                off += 2
                key = block_data[off:off+key_len].decode('utf-8', errors='replace')
                off += key_len
                val_len = read_uint16(block_data, off)
                off += 2
                value = block_data[off:off+val_len].decode('utf-8', errors='replace')
                off += val_len
                tranc_id = read_uint64(block_data, off)
                entries.append([idx, key, value, tranc_id])
        except (struct.error, IndexError) as e:
            print(f"警告: 无法解析Block {i}。错误: {e}")
            continue

        block_tables.append(html_table(entries, ["索引", "键", "值", "事务ID"]))

    extra_html = html_table([
        ["Meta Section Offset", meta_section_offset],
        ["Bloom Filter Offset", bloom_offset],
        ["Min Transaction ID", min_tranc_id],
        ["Max Transaction ID", max_tranc_id]
    ], ["字段", "值"])

    meta_html = html_table(meta_rows, ["Block 索引", "Block 偏移量", "首键", "尾键"])

    bloom_html = parse_bloom_filter(data[bloom_section_start:bloom_section_end])
#
    block_nav = ''
    block_tables_html = ''
    for i, table in enumerate(block_tables):
        active_class_btn = 'active' if i == 0 else ''
        block_nav += f'<button class="block-btn {active_class_btn}" id="block-btn-{i}" onclick="showBlock({i})">Block {i}</button>'
        
        active_class_table = 'active' if i == 0 else ''
        block_tables_html += f'<div class="block-table {active_class_table}" id="block-table-{i}">{table}</div>'

    sidebar_html = ""
    layout_for_sidebar = sorted(boundaries.items(), key=lambda item: item[1]['start'])
    
    section_titles = {
        'blocks': 'Block Section', 'meta': 'Meta Section',
        'bloom': 'Bloom Filter', 'extra': 'Extra Section'
    }

    for section_id, props in layout_for_sidebar:
        if props['size'] > 0:
            percentage = (props['size'] / file_size) * 100
            title = section_titles.get(section_id, section_id.title())
            sidebar_html += f'''
            <div class="sidebar-segment" id="nav-{section_id}" onclick="showSection('{section_id}')">
                <h3>{title}</h3>
                <div class="segment-info">
                    <span>{props['size']:,} bytes</span>
                    <span>{percentage:.2f}%</span>
                </div>
            </div>'''
            
    html_template = get_html_template()
    final_html = html_template.format(
        filename=os.path.basename(filename), sidebar_html=sidebar_html,
        block_section_end=f"{block_section_end:,}", block_section_size=f"{block_section_size:,}",
        num_blocks=len(block_tables), block_nav=block_nav, block_tables=block_tables_html,
        meta_block_offset=f"{meta_section_start:,}", meta_section_end=f"{meta_section_end:,}",
        meta_section_size=f"{meta_section_size:,}", num_meta_entries=num_meta_entries, meta_html=meta_html,
        bloom_offset=f"{bloom_section_start:,}", bloom_section_end=f"{bloom_section_end:,}",
        bloom_section_size=f"{bloom_section_size:,}", bloom_html=bloom_html,
        extra_offset=f"{extra_offset:,}", file_size=f"{file_size:,}",
        extra_len=f"{extra_len:,}", extra_html=extra_html
    )

    output_dir = "sst_html"
    os.makedirs(output_dir, exist_ok=True)
    output_path = os.path.join(output_dir, f"{os.path.basename(filename)}_visualization.html")
    with open(output_path, "w", encoding='utf-8') as f:
        f.write(final_html)
    print(f"可视化文件已成功生成: {os.path.abspath(output_path)}")


def get_html_template():
    """返回带有左右布局和精简样式的完整HTML模板。"""
    return """
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SST 文件可视化</title>
    <style>
        body {{
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            margin: 0; padding: 20px 40px; background-color: #f8f9fa; color: #212529;
            display: flex; flex-direction: column; height: 100vh;
        }}
        h1 {{ text-align: center; margin-bottom: 20px; flex-shrink: 0; }}
        h2, h3 {{ color: #343a40; margin-top: 1.5em; margin-bottom: 0.5em; }}
        .main-container {{ display: flex; gap: 25px; flex-grow: 1; overflow: hidden; }}
        .left-sidebar {{ flex: 0 0 300px; overflow-y: auto; padding: 15px; }}
        .sidebar-segment {{
            padding: 15px 20px; margin-bottom: 15px; border-radius: 8px; cursor: pointer;
            transition: all 0.2s ease-in-out; border: 2px solid transparent;
            color: white; text-shadow: 1px 1px 2px rgba(0,0,0,0.4);
        }}
        .sidebar-segment h3 {{ margin: 0 0 10px 0; padding-bottom: 5px; border-bottom: 1px solid rgba(255,255,255,0.5); color: white; }}
        .segment-info {{ display: flex; justify-content: space-between; font-size: 14px; }}
        .sidebar-segment:hover {{ transform: translateY(-2px); box-shadow: 0 8px 15px rgba(0,0,0,0.1); }}
        .sidebar-segment.active {{ border-color: #000; box-shadow: 0 0 10px rgba(0,0,0,0.2); transform: scale(1.02); }}
        #nav-blocks {{ background: linear-gradient(135deg, #0d6efd, #0a58ca); }}
        #nav-meta {{ background: linear-gradient(135deg, #198754, #146c43); }}
        #nav-bloom {{ background: linear-gradient(135deg, #fd7e14, #d96c0f); }}
        #nav-extra {{ background: linear-gradient(135deg, #6c757d, #5a6268); }}
        .right-content {{ flex-grow: 1; overflow-y: auto; }}
        .detail-panel {{ display: none; padding: 20px; border: 1px solid #dee2e6; border-radius: 8px; background-color: #ffffff; box-shadow: 0 4px 8px rgba(0,0,0,0.05); }}
        .detail-panel.active {{ display: block; }}
        table.data-table {{ width: 100%; border-collapse: collapse; margin-top: 15px; }}
        .data-table th, .data-table td {{ border: 1px solid #dee2e6; padding: 10px; text-align: left; vertical-align: top; word-break: break-word; }}
        .data-table th {{ background-color: #e9ecef; }}
        .data-table tbody tr:nth-child(even) {{ background-color: #f8f9fa; }}
        .hex-preview {{ font-family: monospace; font-size: 14px; background: #e9ecef; padding: 15px; border-radius: 4px; word-break: break-all; white-space: pre-wrap; max-height: 400px; overflow-y: auto; }}
        .block-nav {{ margin-bottom: 1em; padding-bottom: 1em; border-bottom: 1px solid #dee2e6; display: flex; flex-wrap: wrap; gap: 8px; }}
        .block-btn {{ padding: 8px 12px; border: 1px solid #0d6efd; background: white; color: #0d6efd; border-radius: 5px; cursor: pointer; transition: all 0.2s; }}
        .block-btn:hover {{ background: #e7f3ff; }}
        .block-btn.active {{ background: #0d6efd; color: white; }}
        .block-table {{ display: none; }}
        .block-table.active {{ display: block; }}
        .info-grid {{ display: grid; grid-template-columns: auto 1fr; gap: 10px 20px; background-color: #f8f9fa; padding: 15px; border-radius: 5px; margin-bottom: 15px; }}
        .info-grid > div:nth-child(odd) {{ font-weight: bold; color: #495057; }}
        .bloom-meta-table td:first-child {{ font-weight: 500; }}
    </style>
    <script>
        function showSection(sectionId) {{
            document.querySelectorAll('.detail-panel').forEach(panel => panel.classList.remove('active'));
            const activePanel = document.getElementById('detail-' + sectionId);
            if (activePanel) activePanel.classList.add('active');
            document.querySelectorAll('.sidebar-segment').forEach(segment => segment.classList.remove('active'));
            const activeSegment = document.getElementById('nav-' + sectionId);
            if (activeSegment) activeSegment.classList.add('active');
        }}
        function showBlock(idx) {{
            const container = document.getElementById('detail-blocks');
            container.querySelectorAll('.block-table').forEach(table => table.classList.remove('active'));
            const activeTable = document.getElementById('block-table-' + idx);
            if (activeTable) activeTable.classList.add('active');
            container.querySelectorAll('.block-btn').forEach(btn => btn.classList.remove('active'));
            const activeButton = document.getElementById('block-btn-' + idx);
            if (activeButton) activeButton.classList.add('active');
        }}
        window.onload = function() {{
            const firstSegment = document.querySelector('.sidebar-segment');
            if (firstSegment) {{ showSection(firstSegment.id.replace('nav-', '')); }}
        }};
    </script>
</head>
<body>
    <h1>SST 文件可视化: {filename}</h1>
    <div class="main-container">
        <div class="left-sidebar">{sidebar_html}</div>
        <div class="right-content">
            <div id="detail-blocks" class="detail-panel">
                <h2>Block Section</h2>
                <div class="info-grid">
                    <div>范围 (bytes):</div><div>0 - {block_section_end}</div>
                    <div>大小:</div><div>{block_section_size} bytes</div>
                    <div>Block 数量:</div><div>{num_blocks}</div>
                </div>
                <h3>Block 导航</h3>
                <div class="block-nav">{block_nav}</div>
                <div class="block-content">{block_tables}</div>
            </div>
            <div id="detail-meta" class="detail-panel">
                <h2>Meta Section</h2>
                <div class="info-grid">
                    <div>范围 (bytes):</div><div>{meta_block_offset} - {meta_section_end}</div>
                    <div>大小:</div><div>{meta_section_size} bytes</div>
                    <div>条目数量:</div><div>{num_meta_entries}</div>
                </div>
                {meta_html}
            </div>
            <div id="detail-bloom" class="detail-panel">
                <h2>Bloom Filter Section</h2>
                <div class="info-grid">
                    <div>范围 (bytes):</div><div>{bloom_offset} - {bloom_section_end}</div>
                    <div>大小:</div><div>{bloom_section_size} bytes</div>
                </div>
                <h3>元数据 (Metadata)</h3>
                {bloom_html}
            </div>
            <div id="detail-extra" class="detail-panel">
                <h2>Extra Section</h2>
                <div class="info-grid">
                    <div>范围 (bytes):</div><div>{extra_offset} - {file_size}</div>
                    <div>大小:</div><div>{extra_len} bytes</div>
                </div>
                {extra_html}
            </div>
        </div>
    </div>
</body>
</html>
"""

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("使用方法: python sst_visualize.py <sst_file_path>")
        sys.exit(1)

    sst_file_path = sys.argv[1]
    if not os.path.exists(sst_file_path):
        print(f"错误: 文件 '{sst_file_path}' 不存在。")
        sys.exit(1)

    parse_sst(sst_file_path)