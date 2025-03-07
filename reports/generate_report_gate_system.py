import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import seaborn as sns
import os

# Load the CSV data
df = pd.read_csv('memory_gate_system.csv')

# Convert timestamp to numeric if it's not already
df['timestamp'] = pd.to_numeric(df['timestamp'])

# Add a calculated column for memory delta (useful for some plots)
df['memory_delta'] = df['total_allocated_bytes'].diff()

# Extract initialization data and gate operation data
init_data = df[df['event'].str.contains('Before|After (WiFi|MQTT|Servo) init')]
gate_ops = df[df['event'].str.contains('(Before|After) open entry gate|(Before|After) close entry gate')]

# Create operation pairs (before/after) for gate operations
gate_ops_before = gate_ops[gate_ops['event'].str.contains('Before')]
gate_ops_after = gate_ops[gate_ops['event'].str.contains('After')]

# Make sure we have the same number of before/after pairs
min_ops = min(len(gate_ops_before), len(gate_ops_after))
gate_ops_before = gate_ops_before.iloc[:min_ops].reset_index(drop=True)
gate_ops_after = gate_ops_after.iloc[:min_ops].reset_index(drop=True)

# Calculate memory used per operation
gate_ops_memory = pd.DataFrame({
    'operation_number': range(1, min_ops + 1),
    'free_heap_before': gate_ops_before['free_heap'].values,
    'free_heap_after': gate_ops_after['free_heap'].values,
    'allocated_before': gate_ops_before['total_allocated_bytes'].values,
    'allocated_after': gate_ops_after['total_allocated_bytes'].values
})

gate_ops_memory['memory_used'] = gate_ops_memory['free_heap_before'] - gate_ops_memory['free_heap_after']
gate_ops_memory['allocation_increase'] = gate_ops_memory['allocated_after'] - gate_ops_memory['allocated_before']

# 1. Memory Usage Timeline
plt.figure(figsize=(14, 8))
plt.plot(df['timestamp'], df['free_heap'], marker='o', linestyle='-', color='blue')
plt.title('Memory Usage Timeline for Gate System', fontsize=16)
plt.xlabel('Time (ms)', fontsize=12)
plt.ylabel('Free Heap (bytes)', fontsize=12)
plt.grid(True, alpha=0.3)

# Add annotations for key events
for _, row in df.iterrows():
    if 'init' in row['event']:
        plt.annotate(row['event'], 
                    (row['timestamp'], row['free_heap']),
                    textcoords="offset points",
                    xytext=(0,10),
                    ha='center',
                    fontsize=8,
                    rotation=45)

# Add a horizontal line for the minimum free heap
plt.axhline(y=df['min_free_heap'].iloc[0], color='r', linestyle='--', alpha=0.7)
plt.text(df['timestamp'].max() * 0.9, df['min_free_heap'].iloc[0] * 1.01, 
        f"Min Free Heap: {df['min_free_heap'].iloc[0]} bytes", 
        color='r', fontsize=10)

plt.tight_layout()
plt.savefig('memory_timeline_gate_system.png', dpi=300)
plt.close()

# 2. Memory Impact by Operation (Bar Chart)
# Extract initialization events
init_events = df[df['event'].str.contains('Before|After')].copy()
init_events['operation'] = init_events['event'].str.extract(r'(Before|After)(.+)')
init_events['phase'] = init_events['operation'].str.get(0)
init_events['operation_name'] = init_events['operation'].str.get(1).str.strip()

# Create a pivot table for initialization impact
init_pivot = pd.pivot_table(
    init_events,
    index='operation_name',
    columns='phase',
    values='free_heap',
    aggfunc='mean'
).reset_index()

# Calculate the delta
if 'Before' in init_pivot.columns and 'After' in init_pivot.columns:
    init_pivot['Memory Impact'] = init_pivot['Before'] - init_pivot['After']
    init_pivot = init_pivot.sort_values('Memory Impact', ascending=False)

    plt.figure(figsize=(12, 7))
    bars = plt.bar(init_pivot['operation_name'], init_pivot['Memory Impact'], color='skyblue')
    plt.title('Memory Impact by Operation for Gate System', fontsize=16)
    plt.xlabel('Operation', fontsize=12)
    plt.ylabel('Memory Impact (bytes)', fontsize=12)
    plt.grid(axis='y', alpha=0.3)
    plt.xticks(rotation=45, ha='right')

    # Add value labels on top of bars
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2., height + 500,
                f'{int(height)}',
                ha='center', va='bottom', rotation=0, fontsize=10)

    plt.tight_layout()
    plt.savefig('memory_impact_by_operation_gate_system.png', dpi=300)
    plt.close()

# 3. Memory Recovery Pattern
plt.figure(figsize=(14, 7))
plt.plot(gate_ops_memory['operation_number'], gate_ops_memory['free_heap_before'], 
        marker='o', linestyle='-', label='Before Operation', color='green')
plt.plot(gate_ops_memory['operation_number'], gate_ops_memory['free_heap_after'], 
        marker='x', linestyle='--', label='After Operation', color='red')

plt.title('Memory Recovery Pattern Across Gate Operations', fontsize=16)
plt.xlabel('Operation Number', fontsize=12)
plt.ylabel('Free Heap (bytes)', fontsize=12)
plt.legend(fontsize=10)
plt.grid(True, alpha=0.3)
plt.tight_layout()
plt.savefig('memory_recovery_pattern_gate_system.png', dpi=300)
plt.close()

# 4. Allocated vs. Free Memory
plt.figure(figsize=(14, 7))
plt.stackplot(df['timestamp'], 
            df['total_allocated_bytes'], 
            df['free_heap'],
            labels=['Allocated Memory', 'Free Memory'],
            colors=['#ff9999', '#66b3ff'])

plt.title('Memory Composition Over Time in Gate System', fontsize=16)
plt.xlabel('Time (ms)', fontsize=12)
plt.ylabel('Memory (bytes)', fontsize=12)
plt.legend(loc='upper right', fontsize=10)
plt.grid(True, alpha=0.3)
plt.tight_layout()
plt.savefig('allocated_vs_free_memory_gate_system.png', dpi=300)
plt.close()

# 5. Memory Fragmentation Analysis
plt.figure(figsize=(14, 7))
plt.plot(df['timestamp'], df['free_heap'], marker='', linestyle='-', 
        label='Total Free Bytes', color='blue')
plt.plot(df['timestamp'], df['largest_free_block'], marker='', linestyle='-', 
        label='Largest Free Block', color='orange')
plt.fill_between(df['timestamp'], df['free_heap'], df['largest_free_block'], 
                alpha=0.3, color='red', label='Fragmentation')

# Calculate and display fragmentation percentage
df['fragmentation_percent'] = (1 - (df['largest_free_block'] / df['free_heap'])) * 100

plt.title('Memory Fragmentation Analysis for Gate System', fontsize=16)
plt.xlabel('Time (ms)', fontsize=12)
plt.ylabel('Memory (bytes)', fontsize=12)
plt.legend(fontsize=10)
plt.grid(True, alpha=0.3)

# Add average fragmentation text
avg_frag = df['fragmentation_percent'].mean()
plt.figtext(0.5, 0.01, f'Average Fragmentation: {avg_frag:.2f}%', 
            ha='center', fontsize=12, bbox={'facecolor':'white', 'alpha':0.8, 'pad':5})

plt.tight_layout(rect=[0, 0.03, 1, 0.97])  # Make room for the text at the bottom
plt.savefig('memory_fragmentation_gate_system.png', dpi=300)
plt.close()

# 6. Gate Operation Memory Delta
plt.figure(figsize=(12, 7))
bars = plt.bar(gate_ops_memory['operation_number'], gate_ops_memory['memory_used'], color='purple')
plt.title('Memory Consumption per Gate Operation', fontsize=16)
plt.xlabel('Operation Number', fontsize=12)
plt.ylabel('Memory Used (bytes)', fontsize=12)
plt.grid(axis='y', alpha=0.3)

# Add average line
avg_mem_used = gate_ops_memory['memory_used'].mean()
plt.axhline(y=avg_mem_used, color='red', linestyle='--')
plt.text(len(gate_ops_memory) * 0.8, avg_mem_used * 1.05, 
        f'Average: {avg_mem_used:.2f} bytes', color='red')

# Add value labels on top of bars
for bar in bars:
    height = bar.get_height()
    plt.text(bar.get_x() + bar.get_width()/2., height + 50,
            f'{int(height)}',
            ha='center', va='bottom', rotation=0, fontsize=9)

plt.tight_layout()
plt.savefig('gate_operation_memory_delta_gate_system.png', dpi=300)
plt.close()

# 7. System Summary Tables as visualizations
# Initialization Summary Table
init_summary = pd.DataFrame({
    'Operation': ['WiFi Init', 'Servo Init', 'MQTT Init', 'Total System Init'],
    'Before (bytes)': [
        df[df['event'] == 'Before WiFi init']['free_heap'].iloc[0],
        df[df['event'] == 'Before Servo init']['free_heap'].iloc[0],
        df[df['event'] == 'Before MQTT init']['free_heap'].iloc[0],
        df[df['event'] == 'Before WiFi init']['free_heap'].iloc[0]
    ],
    'After (bytes)': [
        df[df['event'] == 'After WiFi init']['free_heap'].iloc[0],
        df[df['event'] == 'After Servo init']['free_heap'].iloc[0],
        df[df['event'] == 'After MQTT init']['free_heap'].iloc[0],
        df[df['event'] == 'After MQTT init']['free_heap'].iloc[0]
    ]
})

init_summary['Delta (bytes)'] = init_summary['Before (bytes)'] - init_summary['After (bytes)']
init_summary['% of Initial Memory'] = (init_summary['Delta (bytes)'] / init_summary['Before (bytes)'].iloc[0]) * 100

# Create a visual table for initialization summary
fig, ax = plt.subplots(figsize=(12, 6))
ax.axis('tight')
ax.axis('off')
table = ax.table(cellText=init_summary.values.round(2), 
                colLabels=init_summary.columns,
                loc='center',
                cellLoc='center')
table.auto_set_font_size(False)
table.set_fontsize(10)
table.scale(1.2, 1.5)
plt.title('System Initialization Memory Impact for Gate System', fontsize=16, pad=20)
plt.tight_layout()
plt.savefig('initialization_summary_table_gate_system.png', dpi=300)
plt.close()

# 8. Operation Memory Analysis Table
operation_summary = pd.DataFrame({
    'Operation': [f'Gate Op #{i+1}' for i in range(len(gate_ops_memory))] + ['Average'],
    'Free Heap Before': list(gate_ops_memory['free_heap_before']) + [gate_ops_memory['free_heap_before'].mean()],
    'Free Heap After': list(gate_ops_memory['free_heap_after']) + [gate_ops_memory['free_heap_after'].mean()],
    'Memory Used': list(gate_ops_memory['memory_used']) + [gate_ops_memory['memory_used'].mean()],
    'Allocated Before': list(gate_ops_memory['allocated_before']) + [gate_ops_memory['allocated_before'].mean()],
    'Allocated After': list(gate_ops_memory['allocated_after']) + [gate_ops_memory['allocated_after'].mean()]
})

# Create a visual table for operation summary
fig, ax = plt.subplots(figsize=(14, 7))
ax.axis('tight')
ax.axis('off')
table = ax.table(cellText=operation_summary.values.round(2), 
                colLabels=operation_summary.columns,
                loc='center',
                cellLoc='center')
table.auto_set_font_size(False)
table.set_fontsize(10)
table.scale(1.2, 1.5)
plt.title('Gate Operation Memory Analysis', fontsize=16, pad=20)
plt.tight_layout()
plt.savefig('operation_summary_table_gate_system.png', dpi=300)
plt.close()

# 9. Generate Memory Fragmentation Analysis Table
frag_data = df.copy()
frag_data['Fragmentation Index'] = (1 - (frag_data['largest_free_block'] / frag_data['free_heap'])) * 100

# Group by general stages
frag_stages = []
if 'Before WiFi init' in frag_data['event'].values:
    frag_stages.append(frag_data[frag_data['event'] == 'Before WiFi init'].iloc[0])
if 'After WiFi init' in frag_data['event'].values:
    frag_stages.append(frag_data[frag_data['event'] == 'After WiFi init'].iloc[0])
if 'After MQTT init' in frag_data['event'].values:
    frag_stages.append(frag_data[frag_data['event'] == 'After MQTT init'].iloc[0])
    
# Add an average for all gate operations
if len(gate_ops) > 0:
    gate_ops_avg = gate_ops.mean(numeric_only=True)
    gate_ops_avg['event'] = 'During Operations'
    gate_ops_avg['Fragmentation Index'] = (1 - (gate_ops_avg['largest_free_block'] / gate_ops_avg['free_heap'])) * 100
    frag_stages.append(gate_ops_avg)

# Create fragmentation summary DataFrame
frag_summary = pd.DataFrame({
    'Operation Stage': [row['event'] for row in frag_stages],
    'Free Heap': [row['free_heap'] for row in frag_stages],
    'Largest Free Block': [row['largest_free_block'] for row in frag_stages],
    'Fragmentation Index (%)': [row['Fragmentation Index'] for row in frag_stages]
})

# Create a visual table for fragmentation summary
fig, ax = plt.subplots(figsize=(12, 6))
ax.axis('tight')
ax.axis('off')
table = ax.table(cellText=frag_summary.values.round(2), 
                colLabels=frag_summary.columns,
                loc='center',
                cellLoc='center')
table.auto_set_font_size(False)
table.set_fontsize(10)
table.scale(1.2, 1.5)
plt.title('Memory Fragmentation Analysis for Gater System', fontsize=16, pad=20)
plt.tight_layout()
plt.savefig('fragmentation_summary_table_gate_system.png', dpi=300)
plt.close()

# 10. Memory Usage Pie Chart - For a specific point (e.g., after initialization)
if 'After MQTT init' in df['event'].values:
    final_init = df[df['event'] == 'After MQTT init'].iloc[0]
    
    plt.figure(figsize=(10, 8))
    plt.pie([final_init['total_allocated_bytes'], final_init['free_heap']], 
            labels=['Allocated Memory', 'Free Memory'],
            autopct='%1.1f%%',
            startangle=90,
            colors=['#ff9999', '#66b3ff'],
            explode=(0.1, 0))
    plt.title('Memory Distribution After System Initialization for Gate System', fontsize=16)
    plt.axis('equal')  # Equal aspect ratio ensures that pie is drawn as a circle
    plt.tight_layout()
    plt.savefig('memory_distribution_pie_gate_system.png', dpi=300)
    plt.close()

print("Analysis complete! Generated plots are saved in reports/.")

# Save summary tables to CSV for further use
init_summary.to_csv('initialization_summary_gate_system.csv', index=False)
operation_summary.to_csv('operation_summary_gate_system.csv', index=False)
frag_summary.to_csv('fragmentation_summary_gate_system.csv', index=False)
