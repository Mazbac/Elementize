import { CheckCircle2 } from 'lucide-react';
import { Badge } from '@/components/ui/badge';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Separator } from '@/components/ui/separator';
import { Table, TableBody, TableCell, TableRow } from '@/components/ui/table';
import type { ElementizeAdminConfig } from '../types';

type Props = { config: ElementizeAdminConfig };

const safety = [
  ['Fresh state required', 'Every write uses a fresh page read and exact expected values.'],
  ['Revision before change', 'A recoverable WordPress revision is required before a content write.'],
  ['Persisted verification', 'Saved Elementor data is verified after each write.'],
  ['Published-page confirmation', 'Live pages require an additional explicit confirmation.'],
] as const;

export function SettingsPage({ config }: Props) {
  return (
    <div className="space-y-5">
      <div>
        <h2 className="text-lg font-semibold">Settings</h2>
        <p className="mt-1 text-sm text-muted-foreground">Most safety settings are always on by design.</p>
      </div>

      <Card>
        <CardHeader>
          <div className="flex items-center justify-between gap-3">
            <div>
              <CardTitle>Safety</CardTitle>
              <CardDescription className="mt-1">Built into every supported write.</CardDescription>
            </div>
            <Badge variant="outline">Always on</Badge>
          </div>
        </CardHeader>
        <CardContent>
          {safety.map(([title, body], index) => (
            <div key={title}>
              {index > 0 && <Separator className="my-3" />}
              <div className="flex items-start gap-3">
                <CheckCircle2 className="mt-0.5 h-4 w-4 shrink-0 text-primary" />
                <div>
                  <p className="font-medium">{title}</p>
                  <p className="mt-1 text-sm text-muted-foreground">{body}</p>
                </div>
              </div>
            </div>
          ))}
        </CardContent>
      </Card>

      <Card>
        <CardHeader>
          <CardTitle>Environment</CardTitle>
          <CardDescription>Detected versions and requirements.</CardDescription>
        </CardHeader>
        <CardContent>
          <Table>
            <TableBody>
              <TableRow>
                <TableCell className="w-[220px] text-muted-foreground">Elementize</TableCell>
                <TableCell className="font-medium">v{config.version}</TableCell>
              </TableRow>
              <TableRow>
                <TableCell className="text-muted-foreground">Elementor</TableCell>
                <TableCell className="font-medium">{config.requirements.elementorDetail}</TableCell>
              </TableRow>
              <TableRow>
                <TableCell className="text-muted-foreground">Pixfort</TableCell>
                <TableCell className="font-medium">{config.requirements.pixfortDetail}</TableCell>
              </TableRow>
              <TableRow>
                <TableCell className="text-muted-foreground">WordPress revisions</TableCell>
                <TableCell className="font-medium">{config.requirements.revisions ? 'Enabled' : 'Needs attention'}</TableCell>
              </TableRow>
            </TableBody>
          </Table>
        </CardContent>
      </Card>
    </div>
  );
}
