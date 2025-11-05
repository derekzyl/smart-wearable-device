"""
Data export routes (CSV/PDF)
"""

from fastapi import APIRouter, Depends, HTTPException, Query
from fastapi.responses import StreamingResponse, FileResponse
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy import select, func
from datetime import datetime, timedelta
from typing import Optional
import csv
import io

# Note: reportlab is optional - install with: pip install reportlab
try:
    from reportlab.lib.pagesizes import letter
    from reportlab.lib import colors
    from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
    from reportlab.platypus import SimpleDocTemplate, Table, TableStyle, Paragraph, Spacer
    from reportlab.lib.units import inch
    REPORTLAB_AVAILABLE = True
except ImportError:
    REPORTLAB_AVAILABLE = False

from database import get_db
from models import VitalsData, User
from routes.auth import get_current_user

router = APIRouter()

@router.get("/csv/{user_id}")
async def export_csv(
    user_id: int,
    start_date: Optional[datetime] = Query(None),
    end_date: Optional[datetime] = Query(None),
    db: AsyncSession = Depends(get_db),
    current_user: User = Depends(get_current_user)
):
    """Export vitals data as CSV"""
    # Verify user access
    if current_user.id != user_id:
        raise HTTPException(status_code=403, detail="Access denied")
    
    # Set default date range if not provided
    if not start_date:
        start_date = datetime.utcnow() - timedelta(days=30)
    if not end_date:
        end_date = datetime.utcnow()
    
    # Query data
    result = await db.execute(
        select(VitalsData)
        .where(VitalsData.user_id == user_id)
        .where(VitalsData.timestamp >= start_date)
        .where(VitalsData.timestamp <= end_date)
        .order_by(VitalsData.timestamp)
    )
    vitals_list = result.scalars().all()
    
    # Create CSV
    output = io.StringIO()
    writer = csv.writer(output)
    
    # Write header
    writer.writerow([
        "Timestamp", "Heart Rate (bpm)", "SpO₂ (%)", "Temperature (°C)",
        "Steps", "Accel X", "Accel Y", "Accel Z"
    ])
    
    # Write data
    for vital in vitals_list:
        writer.writerow([
            vital.timestamp.isoformat(),
            vital.heart_rate or "",
            vital.spo2 or "",
            vital.temperature or "",
            vital.steps or 0,
            vital.accel_x or "",
            vital.accel_y or "",
            vital.accel_z or "",
        ])
    
    output.seek(0)
    
    # Return CSV file
    return StreamingResponse(
        iter([output.getvalue()]),
        media_type="text/csv",
        headers={
            "Content-Disposition": f"attachment; filename=health_data_{user_id}_{datetime.utcnow().strftime('%Y%m%d')}.csv"
        }
    )

@router.get("/pdf/{user_id}")
async def export_pdf(
    user_id: int,
    start_date: Optional[datetime] = Query(None),
    end_date: Optional[datetime] = Query(None),
    db: AsyncSession = Depends(get_db),
    current_user: User = Depends(get_current_user)
):
    """Export vitals data as PDF report"""
    # Check if reportlab is available
    if not REPORTLAB_AVAILABLE:
        raise HTTPException(
            status_code=503,
            detail="PDF export requires reportlab. Install with: pip install reportlab"
        )
    
    # Verify user access
    if current_user.id != user_id:
        raise HTTPException(status_code=403, detail="Access denied")
    
    # Set default date range if not provided
    if not start_date:
        start_date = datetime.utcnow() - timedelta(days=7)
    if not end_date:
        end_date = datetime.utcnow()
    
    # Query data
    result = await db.execute(
        select(VitalsData)
        .where(VitalsData.user_id == user_id)
        .where(VitalsData.timestamp >= start_date)
        .where(VitalsData.timestamp <= end_date)
        .order_by(VitalsData.timestamp)
    )
    vitals_list = result.scalars().all()
    
    # Calculate statistics
    stats_result = await db.execute(
        select(
            func.avg(VitalsData.heart_rate).label("avg_hr"),
            func.max(VitalsData.heart_rate).label("max_hr"),
            func.min(VitalsData.heart_rate).label("min_hr"),
            func.avg(VitalsData.spo2).label("avg_spo2"),
            func.avg(VitalsData.temperature).label("avg_temp"),
            func.sum(VitalsData.steps).label("total_steps"),
        )
        .where(VitalsData.user_id == user_id)
        .where(VitalsData.timestamp >= start_date)
        .where(VitalsData.timestamp <= end_date)
    )
    stats = stats_result.first()
    
    # Create PDF
    buffer = io.BytesIO()
    doc = SimpleDocTemplate(buffer, pagesize=letter)
    story = []
    
    styles = getSampleStyleSheet()
    title_style = ParagraphStyle(
        'CustomTitle',
        parent=styles['Heading1'],
        fontSize=24,
        textColor=colors.HexColor('#1a237e'),
        spaceAfter=30,
    )
    
    # Title
    story.append(Paragraph("Health Data Report", title_style))
    story.append(Spacer(1, 0.2*inch))
    
    # User info
    story.append(Paragraph(f"User: {current_user.full_name or current_user.username}", styles['Normal']))
    story.append(Paragraph(f"Period: {start_date.strftime('%Y-%m-%d')} to {end_date.strftime('%Y-%m-%d')}", styles['Normal']))
    story.append(Spacer(1, 0.3*inch))
    
    # Statistics
    story.append(Paragraph("Statistics", styles['Heading2']))
    stats_data = [
        ['Metric', 'Value'],
        ['Average Heart Rate', f"{stats.avg_hr:.1f} bpm" if stats.avg_hr else "N/A"],
        ['Max Heart Rate', f"{stats.max_hr:.1f} bpm" if stats.max_hr else "N/A"],
        ['Min Heart Rate', f"{stats.min_hr:.1f} bpm" if stats.min_hr else "N/A"],
        ['Average SpO₂', f"{stats.avg_spo2:.1f}%" if stats.avg_spo2 else "N/A"],
        ['Average Temperature', f"{stats.avg_temp:.1f}°C" if stats.avg_temp else "N/A"],
        ['Total Steps', f"{stats.total_steps or 0:,}"],
    ]
    stats_table = Table(stats_data, colWidths=[3*inch, 2*inch])
    stats_table.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), colors.grey),
        ('TEXTCOLOR', (0, 0), (-1, 0), colors.whitesmoke),
        ('ALIGN', (0, 0), (-1, -1), 'LEFT'),
        ('FONTNAME', (0, 0), (-1, 0), 'Helvetica-Bold'),
        ('FONTSIZE', (0, 0), (-1, 0), 12),
        ('BOTTOMPADDING', (0, 0), (-1, 0), 12),
        ('BACKGROUND', (0, 1), (-1, -1), colors.beige),
        ('GRID', (0, 0), (-1, -1), 1, colors.black),
    ]))
    story.append(stats_table)
    story.append(Spacer(1, 0.3*inch))
    
    # Sample data table (first 50 records)
    story.append(Paragraph("Sample Data (First 50 Records)", styles['Heading2']))
    table_data = [['Timestamp', 'HR', 'SpO₂', 'Temp', 'Steps']]
    
    for vital in vitals_list[:50]:
        table_data.append([
            vital.timestamp.strftime('%Y-%m-%d %H:%M'),
            f"{vital.heart_rate:.0f}" if vital.heart_rate else "-",
            f"{vital.spo2:.0f}" if vital.spo2 else "-",
            f"{vital.temperature:.1f}" if vital.temperature else "-",
            str(vital.steps or 0),
        ])
    
    if len(vitals_list) > 50:
        table_data.append(['...', '...', '...', '...', f'({len(vitals_list) - 50} more records)'])
    
    data_table = Table(table_data, colWidths=[2*inch, 1*inch, 1*inch, 1*inch, 1*inch])
    data_table.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), colors.grey),
        ('TEXTCOLOR', (0, 0), (-1, 0), colors.whitesmoke),
        ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
        ('FONTNAME', (0, 0), (-1, 0), 'Helvetica-Bold'),
        ('FONTSIZE', (0, 0), (-1, 0), 10),
        ('BOTTOMPADDING', (0, 0), (-1, 0), 12),
        ('BACKGROUND', (0, 1), (-1, -1), colors.beige),
        ('GRID', (0, 0), (-1, -1), 1, colors.black),
        ('FONTSIZE', (0, 1), (-1, -1), 8),
    ]))
    story.append(data_table)
    
    # Build PDF
    doc.build(story)
    buffer.seek(0)
    
    # Return PDF file
    return StreamingResponse(
        io.BytesIO(buffer.read()),
        media_type="application/pdf",
        headers={
            "Content-Disposition": f"attachment; filename=health_report_{user_id}_{datetime.utcnow().strftime('%Y%m%d')}.pdf"
        }
    )

