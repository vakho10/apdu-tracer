import {Component} from '@angular/core';
import {MatButton} from "@angular/material/button";
import {MatIcon} from "@angular/material/icon";
import {ApduTraceService} from '../../services/apdu-trace-service';

@Component({
  selector: 'app-taskbar',
  imports: [
    MatButton,
    MatIcon
  ],
  templateUrl: './taskbar.html',
  styleUrl: './taskbar.scss'
})
export class Taskbar {

  constructor(private apduTraceService: ApduTraceService) {
  }

  startStop() {
    console.log('Start/Stop tracing...');
  }

  clear() {
    this.apduTraceService.clearTraces();
  }
}
