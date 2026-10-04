import { AudioService } from './AudioService';

// Lets the ability (which owns the app lifecycle) reach the listening service created by the UI.
export class ServiceHub {
  static audio: AudioService | undefined = undefined;
  // The speech recogniser only works on screen: the page stops it in the background and resumes it in the foreground.
  static onForeground: () => void = (): void => {};
  static onBackground: () => void = (): void => {};
  // A touch inside a dialog (they are separate windows): the tap is heard by the microphone as a knock.
  static onTouch: () => void = (): void => {};
}
