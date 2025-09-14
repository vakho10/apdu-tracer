import {Component} from '@angular/core';
import {Toolbar} from './components/toolbar/toolbar';
import {Menubar} from './components/menubar/menubar';
import {ApduList} from './components/apdu-list/apdu-list';
import {Taskbar} from './components/taskbar/taskbar';

@Component({
  selector: 'app-root',
  imports: [
    Toolbar,
    Menubar,
    ApduList,
    Taskbar
  ],
  templateUrl: './app.html',
  styleUrl: './app.scss'
})
export class App {
}
