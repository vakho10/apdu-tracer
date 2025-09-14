import {Injectable, signal} from '@angular/core';

export interface ApduTrace {
  timestamp: string;
  block_type: string;
  apdu_type: 'APDU_REQUEST' | 'APDU_RESPONSE';
  command?: string;
  data: string;
  sw?: string; // only for responses
}

@Injectable({
  providedIn: 'root'
})
export class ApduTraceService {
  private traces = signal<ApduTrace[]>([
    {
      timestamp: '12:03:53.953',
      block_type: 'DATA_BLOCK',
      apdu_type: 'APDU_RESPONSE',
      data: '62338201388302ADF1840DE828BD080FF2504F5420415750A1188C0A7F33FFFFFF00001201339C0A7F33FFFFFF00001201338A',
      sw: '9000'
    },
    {
      timestamp: '12:03:53.954',
      block_type: 'TRANSFER_BLOCK',
      apdu_type: 'APDU_REQUEST',
      command: 'SELECT_BY_DF_NAME_OR_AID',
      data: '00A404041051534344204170706C69636174696F6E00'
    }
  ]);

  constructor() {
    // start simulating new traces
    this.simulateTraces();
  }

  getTraces() {
    return this.traces;
  }

  addTrace(trace: ApduTrace) {
    this.traces.update((arr) => [...arr, trace]);
  }

  clearTraces() {
    this.traces.set([]);
  }


  private simulateTraces() {
    let counter = 0;
    setInterval(() => {
      const now = new Date();
      const timestamp = now.toLocaleTimeString('en-US', {hour12: false}) + '.' + now.getMilliseconds();

      if (counter % 2 === 0) {
        // Simulate REQUEST
        this.addTrace({
          timestamp,
          block_type: 'TRANSFER_BLOCK',
          apdu_type: 'APDU_REQUEST',
          command: 'READ_BINARY',
          data: '00B000000A'
        });
      } else {
        // Simulate RESPONSE
        this.addTrace({
          timestamp,
          block_type: 'DATA_BLOCK',
          apdu_type: 'APDU_RESPONSE',
          data: '6F238408A0000000031010A517BF0C14',
          sw: '9000'
        });
      }

      counter++;
    }, 2000); // every 2 seconds
  }
}
