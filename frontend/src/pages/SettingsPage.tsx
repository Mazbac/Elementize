import { CheckCircle2 } from 'lucide-react';
import { Badge } from '@/components/ui/badge';
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from '@/components/ui/card';
import { Table, TableBody, TableCell, TableRow } from '@/components/ui/table';
import type { ElementizeAdminConfig } from '../types';

type Props = { config: ElementizeAdminConfig };

const safety = [
  ['Fresh state required', 'Every write is based on a fresh page read and exact expected values.'],
  ['Revision before change', 'Elementize requires a recoverable WordPress revision before a content write.'],
  ['Persisted verification', 'Changes are checked against saved Elementor data, not only memory.'],
  ['Published-page confirmation', 'Live pages require an additional explicit confirmation before editing.'],
] as const;

export function SettingsPage({ config }: Props) {
  return (
    <div className="space-y-5">
      <div>
        <h2 className="text-lg font-semibold">Settings</h2>
        <p className="mt-1 max-w-2xl text-sm text-muted-foreground">
          Elementize keeps the important safeguards on by design, so there is very little to configure.
        </p>
      </div>

      <section>
        <div className="mb-3 flex items-center gap-2">
          <h3 className="text-sm font-semibold">Safety model</h3>
          <Badge variant="outline">Always on</Badge>
        </div>
        <div className="grid gap-3 md:grid-cols-2">
          {safety.map(([title, body]) => (
            <Card key={title}>
              <CardHeader className="flex-row items-start gap-3 space-y-0 pb-3">
                <div className="flex h-8 w-8 shrink-0 items-center justify-center rounded-md bg-secondary text-primary">
                  <CheckCircle2 className="h-4 w-4" />
                </div>
                <div>
                  <CardTitle className="text-sm">{title}</CardTitle>
                  <CardDescription className="mt-1 leading-5">{body}</CardDescription>
                </div>
              </CardHeader>
            </Card>
          ))}
        </div>
      </section>

      <Card>
        <CardHeader>
          <CardTitle>Environment</CardTitle>
          <CardDescription>Versions and requirements detected by Elementize.</CardDescription>
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
