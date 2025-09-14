import {AfterViewChecked, Component, ElementRef, signal, ViewChild} from '@angular/core';
import {ApduTraceService} from '../../services/apdu-trace-service';
import {MatCard, MatCardContent, MatCardHeader, MatCardSubtitle, MatCardTitle} from '@angular/material/card';
import {MatDivider} from '@angular/material/divider';
import {MatIcon} from '@angular/material/icon';
import {MatSlideToggle} from '@angular/material/slide-toggle';

@Component({
  selector: 'app-apdu-list',
  imports: [
    MatCard,
    MatCardContent,
    MatCardHeader,
    MatCardSubtitle,
    MatCardTitle,
    MatDivider,
    MatIcon,
    MatSlideToggle
  ],
  templateUrl: './apdu-list.html',
  styleUrl: './apdu-list.scss'
})
export class ApduList implements AfterViewChecked {
  @ViewChild('container') container!: ElementRef<HTMLDivElement>;

  traces;
  autoScroll = signal(true); // <-- signal to control auto-scroll

  constructor(private apduService: ApduTraceService) {
    this.traces = this.apduService.getTraces();
  }

  ngAfterViewChecked() {
    if (this.container && this.autoScroll()) {
      this.container.nativeElement.scrollTop = this.container.nativeElement.scrollHeight;
    }
  }
}
