"""
TeethBrushReminder - A simple reminder application for teeth brushing
"""

import schedule
import time
from datetime import datetime
from plyer import notification


def remind_brush_teeth():
    """Send a notification to remind user to brush teeth"""
    notification.notify(
        title="Teeth Brush Reminder",
        message="Time to brush your teeth! 🦷",
        timeout=10
    )
    print(f"[{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] Reminder sent: Brush your teeth!")


def main():
    """Main function to set up and run reminders"""
    print("TeethBrushReminder started!")
    print("Reminders will be sent at:")
    print("- Morning: 07:00")
    print("- Evening: 21:00")
    print("\nPress Ctrl+C to stop...\n")
    
    # Schedule morning reminder
    schedule.every().day.at("07:00").do(remind_brush_teeth)
    
    # Schedule evening reminder
    schedule.every().day.at("21:00").do(remind_brush_teeth)
    
    # Run the scheduler
    try:
        while True:
            schedule.run_pending()
            time.sleep(60)  # Check every minute
    except KeyboardInterrupt:
        print("\n\nTeethBrushReminder stopped.")


if __name__ == "__main__":
    main()

