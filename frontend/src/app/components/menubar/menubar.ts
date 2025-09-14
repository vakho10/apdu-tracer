import {Component} from '@angular/core';
import {MatButton} from "@angular/material/button";
import {MatDivider} from "@angular/material/divider";
import {MatMenu, MatMenuItem, MatMenuTrigger} from "@angular/material/menu";
import {MatToolbar} from "@angular/material/toolbar";

@Component({
  selector: 'app-menubar',
  imports: [
    MatButton,
    MatDivider,
    MatMenu,
    MatMenuItem,
    MatToolbar,
    MatMenuTrigger
  ],
  templateUrl: './menubar.html',
  styleUrl: './menubar.scss'
})
export class Menubar {
  saveToFile() {
    console.log('Saving to file...');
  }

  close() {
    console.log('Closing...');
  }
}
