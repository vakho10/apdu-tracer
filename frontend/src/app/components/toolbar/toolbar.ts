import {Component, signal} from '@angular/core';
import {MatButton} from '@angular/material/button';
import {MatCheckbox} from '@angular/material/checkbox';
import {MatFormField, MatLabel} from '@angular/material/form-field';
import {MatIcon} from '@angular/material/icon';
import {MatOption, MatSelect} from '@angular/material/select';

@Component({
  selector: 'app-toolbar',
  imports: [
    MatButton,
    MatCheckbox,
    MatFormField,
    MatIcon,
    MatLabel,
    MatOption,
    MatSelect
  ],
  templateUrl: './toolbar.html',
  styleUrl: './toolbar.scss'
})
export class Toolbar {

  skipBulk = signal(true);
  skipUnknown = signal(true);

  interfaces = signal(['Interface1', 'Interface2', 'Interface3']);
  selectedInterface = signal<string | null>(null);

  refreshInterfaces() {
    console.log('Refreshing interfaces...');
  }
}
